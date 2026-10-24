# Networking guide

How multiplayer works in terraria-3d and how to use it from game code.
Code lives in [`src/net/`](../src/net/); this guide explains the design and the usage patterns, the headers hold the exact API.

**Contents**
1. [Architecture at a glance](#1-architecture-at-a-glance)
2. [Vocabulary: ids, channels, packets](#2-vocabulary-ids-channels-packets)
3. [Quick start](#3-quick-start)
4. [The join flow and the session lifecycle](#4-the-join-flow-and-the-session-lifecycle)
5. [Server-side usage](#5-server-side-usage)
6. [Client-side usage](#6-client-side-usage)
7. [Packets: catalog, adding your own, channels, sizes](#7-packets)
8. [Transports: Steam, ENet, Loopback](#8-transports)
9. [Deployment shapes: singleplayer, host, integrated thread, dedicated server](#9-deployment-shapes)
10. [Security and validation checklist](#10-security-and-validation-checklist)
11. [Testing and debugging](#11-testing-and-debugging)
12. [Troubleshooting](#12-troubleshooting)
13. [Known limitations and roadmap](#13-known-limitations-and-roadmap)
14. [API cheat sheet](#14-api-cheat-sheet)

---

## 1. Architecture at a glance

The game is **client <-> authoritative server**. The "host" in a hosted game is simply a server running inside the
player's process, plus that player's own client. Steam's "P2P" only describes how a connection is routed through Valve's relay,
not who simulates the world: the host always does.

```
 Game code (world, entities, UI)
        |  poll_session_event / receive_from_client / send / broadcast ...
        v
 NetworkManager ............ one object the game talks to (owns everything below)
   |-- ServerSession ....... handshake, validation, bans, ping/pong, timeouts, flood kicks   (server side)
   |-- ClientSession ....... handshake client, accept/reject/kick reasons, RTT                (client side)
   v
 NetServer / NetClient ..... typed send/receive, per-type inbox, rate limiting, stats
   v
 [ NetSim decorator ] ...... optional: fake latency / jitter / loss for testing
   v
 Backend ................... Steam sockets | ENet (UDP) | Loopback (in-process)
```

Design rules that shape how you use it:

- **Polling, no callbacks, no threads.** `update()` services the sockets and fills per-type queues; you pull packets when your code is ready.
  A `NetServer`/`NetClient`/`NetworkManager` must only be used from the thread that created it.
- **The server is the only authority.** Clients send *intent* (inputs, "I want to dig here"), the server decides and broadcasts the result.
- **One code path.** The host's own client connects to its own server through a loopback listener, exactly like a remote player.
- **Untrusted input everywhere.** Everything received by the server is hostile until validated.

## 2. Vocabulary: ids, channels, packets

### Ids - do not mix them up

| Id | Type | Assigned by | Lifetime | Use it for |
|---|---|---|---|---|
| `PeerId` | `u32` | transport (slot index) | while the connection exists; **reused** afterwards | addressing a connection (`send_to_client`, `kick`) |
| `PlayerId` | `u16` | `ServerSession` (lowest free) | while the player is joined | in-game player slot, sent to clients |
| `EntityId` | `u32` | the game (server) | game decides | referring to entities in packets |
| *identity* | `u64` | transport (verified) | stable across sessions | bans, saves, permissions: **SteamID64 on Steam, 0 on ENet/Loopback** |

Never trust a name or id written inside a packet as proof of who someone is. `NetworkManager::identity(peer)` is the only trustworthy answer.

### Channels

| Channel | Semantics | Use for |
|---|---|---|
| `Reliable` (default) | delivered, ordered | handshake, chat, block edits, inventory, events |
| `Unreliable` | may be dropped; stale data is useless | player input, entity snapshots, ping |
| `Bulk` | reliable, separate lane | chunk streaming, big tables (never blocks gameplay traffic) |

Unreliable packets can be lost, and on some backends reordered: carry a sequence number or tick if order matters.

### Two kinds of packets

| | `WirePacket` (fixed struct) | `BitMessage` (bit-packed) |
|---|---|---|
| Defined in | `packets.hpp` | `messages.hpp` |
| Wire format | `[type byte][struct bytes]` | `[type byte][bit-packed payload]` |
| Good for | small, rare, fixed-size messages | hot or variable-length data (snapshots, block-change batches) |
| Safety | exact size check on receive | `BitReader` can never read out of bounds; counts are validated |
| Same API? | yes: `send(pkt)`, `receive<T>()` | yes |

Large blobs (chunk data) are neither: use raw bytes (`send_bytes_to` / `receive_raw`) with LZ4 (`compress` / `decompress`), see [section 7](#7-packets).

## 3. Quick start

### 3.1 Application wiring (done in [`core/app.cpp`](../src/core/app.cpp))

```cpp
#include "net/network_manager.hpp"
#include "net/steam_platform.hpp"
using namespace t3d::net;

// ORDER MATTERS: Steam must outlive everything that uses its sockets.
SteamPlatform steam;                    // declared first  -> destroyed last
if (!steam.init()) { /* no Steam client: Steam transport unavailable, use ENet / Loopback */ }
steam.parse_command_line(argc, argv);   // understands "+connect_steam <steamid64>"
NetworkManager network;                 // declared second -> destroyed first

network.set_options({.username = "David", .content_hash = registries.hash()});
```

Every frame (client) / every tick (server):

```cpp
steam.update();                                         // Steam callbacks, overlay "Join Game" events
network.update();                                       // sockets + sessions
if (auto host = steam.take_join_request()) {            // friend clicked "Join Game"
    network.connect_steam(*host);
}
```

### 3.2 Host a game / join a game

```cpp
// Host (singleplayer works the same, see section 9):
network.start_host({.kind = TransportKind::Steam, .port = 25565, .max_clients = 8});
steam.publish_join_info(true);        // lets friends click "Join Game" in Steam

// Join:
network.connect_steam(host_steam_id);                  // friends, through the Steam relay
network.connect("203.0.113.5", 25565);                 // ENet by IP
network.connect(Endpoint::loopback(25565));            // same process only
```

`start_host` and `connect` return immediately. Watch `network.state()`; the player is playing when **`network.in_game()`** is true
(the handshake was accepted). If something goes wrong the state returns to `Idle` and `network.last_error()` holds a message you can show.

## 4. The join flow and the session lifecycle

```
client                                         server
  |-- transport connect ----------------------->|  connection event; peer is "Handshaking"
  |-- HandshakePacket{version, content_hash, name} ->|
  |                                              |  checks, in order:
  |                                              |   1. protocol version        -> Reject VersionMismatch
  |                                              |   2. content hash            -> Reject ContentMismatch
  |                                              |   3. banned identity         -> Reject Banned
  |                                              |   4. duplicate login         -> Reject DuplicateLogin
  |                                              |   5. capacity                -> Reject ServerFull
  |                                              |   6. username (sanitised)    -> Reject BadUsername
  |                                              |  OK: peer is "Validated", gets a PlayerId,
  |                                              |      game receives SessionEvent::JoinRequested
  |                                              |  game: accept(peer, info)  or  reject(peer, code, text)
  |<-- HandshakeAcceptPacket{player_id, name, tick_rate, entity_id, spawn, ...} --|  peer is "Active"
  |<== gameplay traffic ==========================>|
  |-- Ping (every 1 s, unreliable) ------------->|
  |<-- Pong ------------------------------------|  client computes smoothed RTT
```

### States

| Side | State | Meaning |
|---|---|---|
| Server (per peer) | Handshaking | connected, waiting for the `HandshakePacket` |
| | Validated | passed all checks, waiting for the game to `accept()` |
| | Active | in the game; its gameplay packets are delivered |
| | Closing | reject/kick sent, waiting for the transport to finish |
| Client | Connecting -> Handshaking -> Active | `ClientSession::State` (`network.in_game()` == Active) |
| | Disconnected | failed or ended; `disconnect_reason()` says why |

### Timeouts and limits

| What | Value | Where |
|---|---|---|
| Transport connect | 5 s | `NetClient::connect` default (the manager uses it) |
| Server waits for the handshake | 5 s | `SessionConfig::handshake_timeout` |
| Server waits for the game to accept | 15 s | `SessionConfig::accept_timeout` |
| Client waits for accept | 10 s | built in |
| Username | max 16 chars, `[A-Za-z0-9 _-]`, others become `_`; duplicates become `Name#2` | `ServerSession` |
| Incoming packet size | 64 KB | `MAX_INCOMING_PACKET_SIZE` |
| Inbox memory | 32 MB total | `MAX_INBOX_BYTES` |
| Rate limit per peer | 500 pkt/s (burst 1000), 256 KB/s (burst 1 MB) | `RateLimit` |
| Flood kick | > 100 rate-limited packets in 1 s | `SessionConfig::max_rate_violations_per_second` |

### Reject codes (`RejectReason`)

`Other`, `VersionMismatch`, `ContentMismatch`, `ServerFull`, `BadUsername`, `DuplicateLogin`, `Banned`, `Timeout`, `Malformed`.
The client receives the code (`ClientSession::reject_code()`) *and* a human readable sentence (`disconnect_reason()`), also copied to `NetworkManager::last_error()`.

### What the player sees when it ends

Any end of session (reject, kick, server shutdown, timeout, network loss) ends with `network.state() == Idle` and an explanation in `network.last_error()`:
`"Version mismatch (server 1, you 2)"`, `"You are banned from this server"`, `"bye bye"` (your kick text), `"connection lost"`, `"connection timed out"`, ...
Show it in the menu after returning to it.

## 5. Server-side usage

### 5.1 Session events: players joining and leaving

```cpp
while (auto ev = network.poll_session_event()) {
    switch (ev->type) {
    case SessionEvent::Type::JoinRequested: {
        // Fields are already validated: ev->username is sanitised + unique, ev->player_id is allocated,
        // ev->identity is the verified SteamID64 (0 on ENet/Loopback), ev->peer addresses the connection.
        auto* player = world.create_player(ev->player_id, ev->username, ev->identity);   // load/create save by identity
        if (!player) {
            network.server_session()->reject(ev->peer, RejectReason::Other, "World is loading, try again");
            break;
        }
        HandshakeAcceptPacket accept{.entity_id = player->entity, .spawn_x = sx, .spawn_y = sy, .spawn_z = sz};
        network.server_session()->accept(ev->peer, accept);      // fills player_id / username / tick_rate for you
        // then: send the world info + initial chunks to ev->peer, tell the others, tell the newcomer about the others
        break;
    }
    case SessionEvent::Type::Left:
        world.remove_player(ev->player_id);                       // save the player, free chunk tickets
        if (ev->was_active) { /* tell the others (PlayerLeftPacket) */ }
        break;                                                    // was_active == false: left before being accepted
    }
}
```

Rules:
- Every `JoinRequested` must be answered with `accept()` or `reject()`; unanswered peers are kicked after 15 s.
- A `Left` event is delivered for **every** peer that got a `JoinRequested`, however it ended (kick, reject, timeout, network loss).
- While a `ServerSession` is attached do not poll `NetServer::poll_connection_event()` yourself; use the session events.

### 5.2 Receiving gameplay packets

```cpp
while (auto m = network.receive_from_client<PlayerInputPacket>()) {
    auto const& [input, peer] = *m;
    auto player = players.by_peer(peer);                  // map PeerId -> player yourself
    if (!player) continue;
    if (!std::isfinite(input.yaw) || !std::isfinite(input.pitch)) continue;   // validate!
    player->queue_input(input);
}
```

`receive_from_client` only ever returns packets from peers that finished the handshake. Packets from anyone else are silently dropped.
For bit-packed messages and raw packets the calls are identical:

```cpp
while (auto m = network.receive_from_client<MultiBlockChange>()) { ... }          // BitMessage
while (auto raw = network.receive_raw_from_client(PacketType::ChunkData)) { ... } // raw bytes + raw->sender
```

Drain every queue you care about each tick. Unread packets stay queued (bounded by the 32 MB inbox cap, after which new ones are dropped).

### 5.3 Sending

```cpp
network.send_to_client(peer, BlockChangePacket{...});                    // one peer
network.broadcast(TimeUpdatePacket{...});                                // every connected peer (see warning)
network.broadcast_except(peer, PlayerJoinedPacket{...});                 // everyone but one
network.broadcast(snapshot_batch, Channel::Unreliable);                  // bit-packed message, unreliable
network.kick(peer, "AFK for 10 minutes");                                // sends the reason, then disconnects
network.server_session()->ban(network.identity(peer));                   // identity-based ban (ignored for identity 0)
```

> **Warning:** `broadcast*` reaches every *connected* peer, including ones that are still handshaking. Do not broadcast world data
> through it before your game is sure about who is Active; for per-player data prefer iterating your own player list and calling `send_to_client`.
> (A `broadcast_active` helper is on the roadmap.)

### 5.4 Per-tick order for a server

```cpp
network.update();                  // 1. receive + sessions
handle_session_events();           // 2. joins/leaves
handle_gameplay_packets();         // 3. inputs, dig/place, chat, ...
simulate_world();                  // 4. fixed timestep (SERVER_TICK_RATE = 30 Hz)
replicate();                       // 5. snapshots, block changes, chunks to players
```

## 6. Client-side usage

```cpp
network.update();

switch (network.state()) {
case NetworkManager::State::Idle:        /* main menu; show network.last_error() if non-empty */        break;
case NetworkManager::State::Connecting:  /* "Connecting..." */                                            break;
case NetworkManager::State::Connected:   /* transport up; if (!network.in_game()) "Joining..." */         break;
case NetworkManager::State::Hosting:     /* we are also the server */                                     break;
}

if (network.in_game()) {
    auto const& info = *network.client_session()->accept_info();    // player_id, final username, tick_rate, spawn...
    while (auto p = network.receive_from_server<BlockChangePacket>())   { world.apply(*p); }
    while (auto s = network.receive_from_server<EntitySnapshotBatch>()) { entities.on_snapshot(*s); }
    while (auto raw = network.receive_raw_from_server(PacketType::ChunkData)) { /* decompress + load chunk */ }

    network.send(PlayerInputPacket{.sequence = seq++, .move_z = 1.0f}, Channel::Unreliable);
    network.send(BlockDigPacket{.x = x, .y = y, .z = z, .action = BlockDigPacket::Action::Start});
    ui.set_ping(network.rtt_ms());
}
```

- `network.send(...)` is a no-op until the transport is connected; send gameplay packets only when `in_game()`.
- `rtt_ms()` is the smoothed Ping/Pong round trip (falls back to the transport's estimate until the first Pong).
- Leaving: `network.disconnect()`. This is graceful: the server sees a normal `Left` event.
- On the host, `in_game()` becomes true once your *own* server accepted your own local client, so the host's game code must handle its own `JoinRequested` first.

## 7. Packets

### 7.1 Current catalog

Sizes of fixed structs include padding. `-->` = client to server, `<--` = server to client, `<->` = both.

| Packet | Kind | Dir | Channel | Bytes | Purpose |
|---|---|---|---|---|---|
| `PingPacket` / `PongPacket` | struct | --> / <-- | Unreliable | 4 | RTT measurement (handled by the session layer) |
| `HandshakePacket` | struct | --> | Reliable | 48 | version, content hash, username (session layer) |
| `HandshakeAcceptPacket` | struct | <-- | Reliable | 56 | player id, final name, tick rate, entity id, spawn |
| `HandshakeRejectPacket` | struct | <-- | Reliable | 65 | `RejectReason` + text |
| `KickPacket` | struct | <-- | Reliable | 64 | reason before the server drops you |
| `PlayerJoinedPacket` / `PlayerLeftPacket` | struct | <-- | Reliable | 52 / 8 | roster changes (the game sends these) |
| `ChatPacket` | struct | <-> | Reliable | 258 | chat; server overwrites `sender_id` |
| `PlayerInputPacket` | struct | --> | Unreliable | 24 | movement/look/actions intent + sequence number |
| `PlayerStateCorrectionPacket` | struct | <-- | Unreliable | 48 | authoritative state + last processed input |
| `EntitySpawnPacket` / `EntityDespawnPacket` | struct | <-- | Reliable | 28 / 4 | entity lifecycle |
| `EntitySnapshotBatch` | **bit** | <-- | Unreliable | ~22 per entity + 3 | many entities per packet, quantised |
| `TimeUpdatePacket` | struct | <-- | Reliable | 16 | world tick / weather |
| `ChunkDataHeader` + bytes | raw | <-- | **Bulk** | 16 + payload | compressed chunk (see 7.4) |
| `ChunkUnloadPacket` | struct | <-- | Reliable | 12 | client may drop a chunk |
| `BlockChangePacket` | struct | <-- | Reliable | 16 | single authoritative block edit |
| `MultiBlockChange` | **bit** | <-- | Reliable | 13 + ~5 per change | all edits of one chunk in one tick |
| `BlockDigPacket` / `BlockPlacePacket` | struct | --> | Reliable | 16 | mining / placing intent |

Still to define when the systems exist: inventory actions/updates, crafting requests, health/buff updates, sound/particle events, weather.

### 7.2 Adding a fixed-size packet

```cpp
// packets.hpp: add the id BEFORE Count
enum class PacketType : std::uint8_t { ..., MyThing, Count };

struct MyThingPacket {
    static constexpr PacketType TYPE = PacketType::MyThing;
    std::uint32_t entity_id = 0;
    float         amount    = 0;
    char          label[24]{};            // fixed-size text, never std::string
};
```

Rules: trivially copyable and standard layout (`WirePacket` concept enforces it), fixed-width types, no pointers/`std::string`/`bool` soup,
default member initialisers. Always NUL-terminate text you receive before using it (`strnlen`). Client and server must be built with the same
ABI for now; if you ever need cross-compiler/endian safety, make the packet a `BitMessage`.

### 7.3 Adding a bit-packed message

```cpp
struct DamageEvent {
    static constexpr PacketType TYPE = PacketType::MyThing;
    std::vector<std::uint32_t> victims;
    float amount = 0;

    void write(BitWriter& w) const {
        w.write_varuint(victims.size());
        for (auto v : victims) { w.write_varuint(v); }
        w.write_quantized(amount, 0.0f, 1000.0f, 12);            // 12 bits, error ~0.12
    }
    bool read(BitReader& r) {
        auto n = r.read_count(64);                               // ALWAYS bound counts coming from the wire
        victims.clear();
        for (std::size_t i = 0; i < n && r.ok(); ++i) { victims.push_back(static_cast<std::uint32_t>(r.read_varuint())); }
        amount = r.read_quantized(0.0f, 1000.0f, 12);
        return r.ok();                                           // false => packet is dropped and logged
    }
};
```

Tips: put quantisation ranges in one place (`messages.hpp`) so both ends agree; varints make small numbers cost one byte; positions use
`write_fixed32(x, POSITION_FRAC_BITS)` (1/256 block).

### 7.4 Chunk data (variable size, compressed, bulk)

```cpp
// server: [ChunkDataHeader][LZ4 payload] in one Bulk message
auto packed = compress(std::as_bytes(std::span{chunk_bytes}));              // typically a lot smaller on terrain
ChunkDataHeader header{.cx = cx, .cy = cy, .cz = cz, .payload_size = static_cast<std::uint32_t>(packed.size())};
std::vector<std::byte> message(sizeof(header) + packed.size());
std::memcpy(message.data(), &header, sizeof(header));
std::memcpy(message.data() + sizeof(header), packed.data(), packed.size());
network.server()->send_bytes_to(peer, PacketType::ChunkData, message, Channel::Bulk);

// client
while (auto raw = network.receive_raw_from_server(PacketType::ChunkData)) {
    if (raw->payload.size() < sizeof(ChunkDataHeader)) { continue; }
    ChunkDataHeader header;
    std::memcpy(&header, raw->payload.data(), sizeof(header));
    if (header.payload_size != raw->payload.size() - sizeof(header)) { continue; }          // validate the header
    auto bytes = decompress(std::as_bytes(std::span{raw->payload}).subspan(sizeof(header)), /*max_size=*/ 128 * 1024);
    if (!bytes) { continue; }                                                               // corrupt or oversized: drop
    // check the chunk coordinates are plausible, then build the chunk from *bytes
}
```

**Packet size limit:** a received packet may be at most 64 KB (`MAX_INCOMING_PACKET_SIZE`, applied on both ends). A raw 32^3 chunk of `u16` is exactly 64 KB, so
compress first, and split or raise the constant if a worst-case compressed chunk could exceed the limit.

### 7.5 Choosing a channel - cheat sheet

| Situation | Channel |
|---|---|
| must arrive, order matters (edits, chat, inventory) | `Reliable` |
| high-frequency state where the newest wins (input, snapshots, ping) | `Unreliable` + sequence/tick number |
| big transfers (chunks) | `Bulk` |

## 8. Transports

Pick with `TransportKind` (server) / `Endpoint` (client). The rest of the code never changes.

| | Steam | ENet | Loopback |
|---|---|---|---|
| Reach | friends by SteamID, through Valve's relay, **no port forwarding** | direct `IP:port` (needs a reachable port) | same process only |
| Identity | verified SteamID64 | 0 | 0 |
| Needs | Steam client running + `SDK`, `SteamPlatform::init()` | nothing | nothing |
| Use for | normal play | dedicated servers, LAN, tests, fallback | singleplayer, integrated server thread, tests |
| Create server | `{.kind = Steam, .port = P}` | `{.kind = Enet, .port = P}` | `{.kind = Loopback, .port = P}` |
| Connect | `Endpoint::steam_relay(id)` / `connect_steam(id)` | `Endpoint::enet(host, port)` | `Endpoint::loopback(P)` |

### 8.1 Steam setup

1. Put the Steamworks SDK in `./sdk` (git-ignored; Valve's license forbids redistributing it). CMake detects it automatically.
   Options: `-DTERRARIA_ENABLE_STEAM=OFF`, `-DSTEAMWORKS_SDK_DIR=...`, `-DTERRARIA_STEAM_APP_ID=<id>` (480 = Spacewar test app until the game has its own).
2. The build copies `libsteam_api.so` / `steam_api64.dll` next to the executable and writes `steam_appid.txt` (dev only; delete it for shipping builds).
3. The Steam client must be running and logged in. If it isn't, `steam.init()` returns false with a reason and the game falls back to ENet.
4. Hosting: `start_host({.kind = Steam})` then `steam.publish_join_info(true)`. Friends see "Join Game"; the click arrives as `steam.take_join_request()`
   (game running) or `+connect_steam <steamid64>` on the command line (game not running).
5. Testing the real relay needs **two different Steam accounts on two machines**. One account cannot connect to itself through the relay.
   Same-account tests use the host's loopback listener, which is what the host's own client uses anyway.

How the Steam backend works: the server listens on a P2P socket (virtual port 0, reached through the relay by SteamID) **and** on a
loopback-only IP socket (`127.0.0.1:<port>`, used by the host's own client). Connections have two lanes (gameplay, bulk) with a 4:1 bandwidth weighting so chunk streaming cannot starve gameplay.

### 8.2 ENet

Direct UDP. The server must be reachable: public IP/port-forward, same LAN, or a VPS. Used by dedicated servers. No encryption (yet), no authenticated identity.

### 8.3 Loopback

In-process queues behind a mutex. Thread-safe, so the integrated server thread and the main thread can use different `NetServer`/`NetClient` objects over it.
`port` is just a name; a second server on the same name fails to start.

## 9. Deployment shapes

### 9.1 Singleplayer

Start a hosted game on the loopback transport. No Steam, no sockets, nothing exposed:

```cpp
network.start_host({.kind = TransportKind::Loopback, .port = 1, .max_clients = 1});
```

To let friends join later, restart as `{.kind = Steam}` (or ENet); the game code does not change.

### 9.2 Hosted multiplayer (current default)

`NetworkManager::start_host({.kind = Steam, ...})`: server and local client in one process, both pumped by `network.update()` on one thread.
Fine to start with. Server simulation and rendering then share a thread, so a heavy server tick costs frame time.

### 9.3 Integrated server on its own thread (planned)

Give the server thread its own `NetServer` + `ServerSession` over Loopback (or Steam/ENet) and keep `NetworkManager` for the client side on the main thread.
This exact shape is exercised by the `cross-thread loopback` test:

```cpp
std::thread server_thread([&] {
    NetServer server;
    ServerSession session{server, SessionConfig{.content_hash = hash}};
    server.start({.kind = TransportKind::Loopback, .port = 1});
    while (!stop) {
        server.update();
        session.update();
        while (auto ev = session.poll_event()) { /* JoinRequested -> session.accept(...), Left -> ... */ }
        while (auto m = server.receive<PlayerInputPacket>()) { if (session.is_active(m->second)) { /* ... */ } }
        /* simulate + replicate with server.send_to / server.broadcast ... */
        std::this_thread::sleep_for(1ms);          // use the fixed tick timer in the real game
    }
});
// main thread: NetworkManager network; network.connect(Endpoint::loopback(1));
```

Rule: each `NetServer`/`NetClient` object belongs to exactly one thread. Cross-thread data goes through the transport, not through shared game state.

### 9.4 Dedicated server

Headless build target without window/renderer: `NetServer` + `ServerSession` over **ENet** (dedicated servers have no Steam login), same loop as 9.3 on the main thread
with a fixed timestep. `App::run_dedicated_server` is still a TODO.

## 10. Security and validation checklist

The transport and session layers handle framing, sizes, rates, versions, names and bans. **Gameplay validation is yours.** For every packet the server acts on:

- [ ] Sender is an Active player (guaranteed by `receive_from_client`) and you mapped `PeerId` to *their* player, not an id from the packet.
- [ ] Numbers are finite and in range (`isfinite`, clamp angles, movement axes in [-1, 1], hotbar slot < 9, ...).
- [ ] Block coordinates are inside the world, **within reach of the player's position**, and the chunk is loaded.
- [ ] Rate and cooldown: digging/placing/attacking no faster than the tool/weapon allows.
- [ ] State makes sense: item exists in the inventory, target block really is what the client claims, not dead/in a menu.
- [ ] Sequence numbers increase; stale or duplicate inputs are ignored.
- [ ] Anything echoed to other players (chat, names) is length-limited and sanitised.
- [ ] Persistent data (saves, bans, permissions) is keyed by `identity`, never by name or a packet field.
- [ ] Nothing a client sends can make the server allocate unbounded memory or loop for unbounded time.

Already enforced for you: max packet size, unknown packet types, wrong-size fixed packets, bad bit-packed data, inbox memory cap, per-peer token-bucket rate limit
(plus kick for persistent flooding), handshake/accept timeouts, protocol and content-hash match, duplicate Steam logins, bans by SteamID.

Client side: also treat the server's data defensively (bounds, sizes) so a broken or malicious server cannot crash the client.

## 11. Testing and debugging

### 11.1 Automated tests

```
cmake --build build --target net_loopback_test && (cd build && ./net_loopback_test)
```

13 groups: bit serialization (incl. hostile input), messages, compression, handshake edge cases (version/content mismatch, bad and duplicate names,
full server, game reject, handshake/accept timeouts, unauthenticated senders), the full stack over ENet, Loopback and Steam, the simulator (latency, ordering under jitter, loss),
flood protection, cross-thread loopback, Steam identity rules (duplicate login, bans, unreachable host). The Steam groups need the Steam client running; without it they are skipped.
Run it after any change in `src/net/`. It takes ~15 s.

### 11.2 Simulating bad networks

```cpp
network.set_options({.simulation = {.latency_ms = 80, .jitter_ms = 20, .loss = 0.05f}});   // before start_host/connect
```

It delays and drops what *this process sends* (loss only affects `Channel::Unreliable`; reliable stays reliable and ordered). To test a symmetric 160 ms round trip,
enable it on both ends; on a hosted game the host's own client is also delayed. The debug window has sliders for it.

### 11.3 Debug window and statistics

`draw_network_window(network, steam)` in [`net_debug_ui.hpp`](../src/net/net_debug_ui.hpp) shows state, ping, traffic, Steam name/id/relay status, and has Host/Join/Disconnect
with the three transports and the simulation sliders. Call it between `ImGui::NewFrame()` and `ImGui::Render()` (hooking it into the overlay is part of the UI work).

`network.stats()` returns bytes/packets in and out for this process; sample it once a second and subtract to show bandwidth. Server-sent counts are approximate for broadcasts (recipient count x size).

### 11.4 Logging

Warnings and errors go through `core/log.hpp` with a `net:` / `session:` / `steam:` prefix: dropped malformed packets, inbox full, flood kicks, connection failures. Watch the console first when something "doesn't arrive".

## 12. Troubleshooting

| Symptom | Likely cause / fix |
|---|---|
| `Steam is not initialised (is the Steam client running?)` | start Steam and log in; `steam.init()` must succeed before using the Steam transport |
| Steam join times out after 5 s | relay still initialising (see `steam.relay_status()`), host not actually hosting, or a firewall. The manager's connect timeout is fixed at 5 s for now |
| `You are already connected to this server` | same Steam account joined twice (not possible through the relay for real players) |
| `Version mismatch (server X, you Y)` | different builds; bump `PROTOCOL_VERSION` only when the wire format changes |
| `Game content does not match the server` | different `content_hash` (data/ files or mods differ) |
| `could not listen on port N (already in use?)` | another server/instance owns the port; pick another or stop the other process |
| `no loopback server on port N` | the Loopback server must be started (same process) before the client connects |
| Packets never arrive on the server | the sender is not Active (handshake not finished or not accepted yet); check `in_game()` and that your code answered `JoinRequested` |
| `net: dropped malformed packet type N` | struct size mismatch between ends (different builds) or a hostile packet |
| `net: dropped packet with invalid size` | packet over 64 KB: compress or split it |
| Peer kicked with `Too many packets` | sending faster than the rate limit; batch (`MultiBlockChange`, `EntitySnapshotBatch`) instead of one packet per event |
| Crash on exit mentioning Steam sockets | `SteamPlatform` destroyed before `NetworkManager`; declare Steam first |
| "Join Game" in Steam does nothing | host must call `publish_join_info(true)`; the game must be pumping `steam.update()` and reading `take_join_request()` |
| Works on LAN, not over the internet (ENet) | port forwarding/firewall: use Steam, or forward the UDP port |

## 13. Known limitations and roadmap

Limitations to keep in mind:

- `NetworkManager::broadcast*` includes peers that are not Active yet (see the warning in 5.3).
- The connection timeout (5 s) is not configurable through `NetworkManager`; Steam relay setup can occasionally take longer on a cold start.
- Maximum packet size is 64 KB (both directions).
- Ban list lives in memory only (`ServerSession::banned()/set_banned()` exist for you to persist it).
- `identity` is 0 on ENet/Loopback, so non-Steam worlds must key players by name (collisions and impersonation are possible there).
- No encryption on ENet. Steam connections are encrypted by Steam.
- Fixed-size packets rely on identical struct layout on both ends (same compiler/ABI).
- The relay between two different Steam accounts and the Windows build are not covered by automated tests.

Roadmap (needs game systems first): chunk streaming queue with per-client bandwidth budget, entity relevance and delta-compressed snapshots,
client-side prediction/reconciliation, `broadcast_active`, Steam lobbies/browser, Steam auth tickets, Steam game-server mode for dedicated servers.
See `TODO.md`, phase 7.

## 14. API cheat sheet

**`NetworkManager`** ([`network_manager.hpp`](../src/net/network_manager.hpp))

| Call | Meaning |
|---|---|
| `set_options(SessionOptions)` | username, content hash, simulation, rate limit; before `start_host`/`connect` |
| `start_host(ServerConfig)` / `connect(Endpoint)` / `connect(host, port)` / `connect_steam(id)` | begin hosting / joining (non-blocking) |
| `disconnect()` | graceful shutdown of everything |
| `update()` | pump sockets and sessions; once per frame/tick |
| `state()`, `is_hosting()`, `is_connected()`, `in_game()`, `last_error()` | status |
| `rtt_ms()`, `stats()`, `client_count()`, `identity(peer)` | diagnostics / identity |
| `send(pkt, channel)` | client to server |
| `receive_from_server<T>()`, `receive_raw_from_server(type)` | client receive |
| `send_to_client(peer, pkt, channel)`, `broadcast(...)`, `broadcast_except(peer, ...)` | server send |
| `receive_from_client<T>()`, `receive_raw_from_client(type)` | server receive (Active peers only) |
| `poll_session_event()` | server: `JoinRequested` / `Left` |
| `kick(peer, reason)` | server |
| `server_session()` / `client_session()` | `accept`, `reject`, `ban`, `unban`, `is_active`, `player_id`, `peer_of`, `username`, `active_count` / `accept_info`, `disconnect_reason`, `reject_code`, `rtt_ms` |
| `server()` / `client()` | raw `NetServer` / `NetClient` (null when not in that role) |

**Types:** `Endpoint` (`enet`, `steam_relay`, `steam_direct`, `loopback`), `ServerConfig{kind, port, max_clients}`,
`SessionOptions{username, content_hash, simulation, rate_limit, allow_duplicate_identity}`, `SessionConfig`, `NetSimConfig{latency_ms, jitter_ms, loss}`,
`RateLimit`, `TrafficStats`, `Channel{Reliable, Unreliable, Bulk}`, `RejectReason`.

**File map**

| File | Role |
|---|---|
| `packets.hpp` | packet ids and fixed-size structs |
| `messages.hpp`, `serialization.hpp` | bit-packed messages, `BitWriter`/`BitReader` |
| `compression.hpp/.cpp` | LZ4 helpers |
| `session.hpp/.cpp` | `ServerSession`, `ClientSession` |
| `network_manager.hpp/.cpp` | the facade the game uses |
| `transport.hpp/.cpp`, `transport_types.hpp`, `transport_backend.hpp` | `NetServer`/`NetClient`, shared types, internal backend interfaces |
| `steam_backend.cpp`, `enet_backend.cpp`, `loopback_backend.cpp`, `sim_backend.cpp` | backends and the simulator |
| `steam_platform.hpp/.cpp` | Steam lifecycle, identity, invites |
| `net_debug_ui.hpp` | ImGui window |
| `server.hpp`, `client.hpp`, `replication.hpp` | design notes for the game-level server/client/replication (not implemented yet) |
| `tests/net_loopback_test.cpp` | the test suite |
