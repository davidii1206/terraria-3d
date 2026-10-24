# net/ - networking

> Full guide (architecture, join flow, usage patterns, packet catalog, security checklist, troubleshooting): [`docs/networking.md`](../../docs/networking.md).
> This file is the short reference.

Typed, poll-based client/server networking with three interchangeable backends:

| Backend | Use | Reachability |
|---|---|---|
| **Steam** (`ISteamNetworkingSockets`) | normal play: friends join the host by SteamID | through Valve's relay, **no port forwarding** |
| **ENet** (UDP) | dedicated servers, LAN, tests, fallback without Steam | direct `IP:port` |
| **Loopback** (in-process) | singleplayer, integrated server thread, tests | same process only, no sockets, thread-safe |

Topology is always **clients <-> one authoritative host/server**. Steam's "P2P" only describes how the connection is routed.
No threads, no locks, no callbacks: call `update()`, then pull packets out.

| File | Purpose |
|---|---|
| `packets.hpp` | `PacketType` ids + fixed-size payload structs (the wire protocol) |
| `messages.hpp`, `serialization.hpp` | bit-packed variable-length messages (`EntitySnapshotBatch`, `MultiBlockChange`), `BitWriter`/`BitReader`, quantisation |
| `compression.hpp/.cpp` | LZ4 for bulk payloads (chunk data) with zip-bomb protection |
| `session.hpp/.cpp` | `ServerSession` / `ClientSession`: handshake, validation, bans, ping/pong, timeouts, flood kicks |
| `loopback_backend.cpp`, `sim_backend.cpp` | in-process transport; latency/jitter/loss simulator (decorator for any backend) |
| `transport.hpp/.cpp` | `NetServer`, `NetClient` - typed API over a backend |
| `transport_types.hpp` | `Endpoint`, `ServerConfig`, `Channel`, `PacketInbox`, ... |
| `transport_backend.hpp` | internal backend interfaces (game code never includes it) |
| `enet_backend.cpp`, `enet_implementation.cpp` | ENet backend (+ the one TU defining `ENET_IMPLEMENTATION`) |
| `steam_backend.cpp` | Steam sockets backend |
| `steam_platform.hpp/.cpp` | `SteamPlatform`: init, callbacks, identity, rich-presence invites, relay status |
| `network_manager.hpp/.cpp` | `NetworkManager`: host / join / disconnect facade used by game code |
| `net_debug_ui.hpp` | ImGui host/join window |
| `server.hpp`, `client.hpp`, `replication.hpp` | game-level design notes (what to send, to whom, when) |

## Using it

```cpp
net::SteamPlatform steam;               // declare BEFORE the manager: it must outlive it
steam.init();                           // false => no Steam client; use ENet
net::NetworkManager network;

network.start_host({.kind = net::TransportKind::Steam});   // friends join by SteamID
steam.publish_join_info(true);                              // enables "Join Game" in the Steam friends list
// or:  network.connect_steam(host_steam_id);  /  network.connect("1.2.3.4", 25565);  /  network.connect(Endpoint{...})

// every frame
steam.update();
network.update();
if (auto host = steam.take_join_request()) network.connect_steam(*host);

// server: the session layer already validated version / content hash / name / bans / capacity
while (auto ev = network.poll_session_event()) {
    if (ev->type == SessionEvent::Type::JoinRequested) {
        // create the player entity, then:
        network.server_session()->accept(ev->peer, HandshakeAcceptPacket{.entity_id = ..., .spawn_x = ...});
        // or: network.server_session()->reject(ev->peer, RejectReason::Other, "World is loading");
    } else { /* Left: remove the player entity (ev->was_active tells if the game ever accepted them) */ }
}
while (auto m = network.receive_from_client<PlayerInputPacket>()) { auto [pkt, peer] = *m; /* only handshaked peers; still validate values! */ }
while (auto p = network.receive_from_server<BlockChangePacket>()) { /* apply to world cache */ }

network.send(PlayerInputPacket{...}, Channel::Unreliable);        // client -> server
network.send_to_client(peer, HandshakeAcceptPacket{...});          // server -> one
network.broadcast_except(peer, PlayerJoinedPacket{...});           // server -> all but one
```

## Setup (Steam)

1. The Steamworks SDK lives in `./sdk` (git-ignored; its license forbids redistribution). CMake detects it automatically
   (`-DTERRARIA_ENABLE_STEAM=OFF` to build without; `-DSTEAMWORKS_SDK_DIR=...` for another location).
2. Build copies `libsteam_api.so` / `steam_api64.dll` next to the executable and writes `steam_appid.txt` (480 = Spacewar test app)
   so the exe also runs outside Steam. For shipping: set `-DTERRARIA_STEAM_APP_ID=<yours>` and delete `steam_appid.txt`.
3. The Steam client must be running and logged in.
4. Invites: when hosting, call `publish_join_info(true)`. A friend's "Join Game" arrives as `take_join_request()` (running game) or
   `+connect_steam <steamid64>` on the command line (game not running).

## The join flow (session layer)

```
client                                server
  connect (transport)  ------------>  connection event
  HandshakePacket{version, content hash, name}  --->  checks: version, content hash, ban list, duplicate login,
                                                       capacity, username (sanitised + unique) -> SessionEvent::JoinRequested
                                      game: accept()  ->  HandshakeAcceptPacket{player id, final name, tick rate, spawn, ...}
  <--- Reject{code, text} | Kick{reason}   (any failure: the player always gets a readable reason)
  Ping/Pong every second -> ClientSession::rtt_ms()
```
Timeouts: no handshake within 5 s, or the game not answering within 15 s, rejects the peer. Peers that exceed the rate limit are dropped
packet-by-packet and kicked if they keep it up. `NetworkManager::receive_from_client` never returns packets from peers that did not finish the handshake.

## Adding a packet

```cpp
// packets.hpp
enum class PacketType : uint8_t { ..., MyThing, Count };   // add before Count

struct MyThingPacket {
    static constexpr PacketType TYPE = PacketType::MyThing;
    uint32_t id = 0;
    char     name[32]{};      // fixed-size text, never std::string
};
```
Rules: trivially copyable, fixed-width types, no pointers. The type byte is added by the transport
(wire format is `[u8 type][struct bytes]`). Hot or variable-length data (snapshots, block-change batches): write a `BitMessage`
(`messages.hpp`: `write(BitWriter&)` / `bool read(BitReader&)`, quantised floats, counts validated with `read_count`) - `send()` / `receive<T>()` work the same.
Large blobs (chunks): `compress()` + `send_bytes_to(...)` on `Channel::Bulk`, `decompress(payload, max_size)` on receipt.

## Behaviour worth knowing

- **Identity**: `identity(peer)` is the transport-verified id (SteamID64 on Steam, 0 on ENet). Use it for bans, saves and
  permissions. Never trust an id/name sent inside a packet.
- `connect()` is non-blocking. `state()` goes `Connecting -> Connected`; on failure it returns to `Idle`
  and `last_error()` says why. Unreachable hosts show up as a timeout (default 5 s).
- Hosting = server + a local client over a loopback listener (one code path for singleplayer and multiplayer).
  With Steam the host's own client uses a loopback-only Steam socket (never the relay) - and its identity is the local user.
- Channels: `Reliable` (ordered), `Unreliable` (inputs/snapshots), `Bulk` (reliable, separate lane so chunk streaming never delays gameplay).
- Peer ids are slot indices and are reused after a disconnect; process the `connected == false` event first.
- Incoming packets are untrusted: wrong-size, unknown-type and oversized packets are dropped and logged, and the inbox is capped (32 MB).
  Still validate every field you read (positions, slots, lengths).
- Teardown order: `NetworkManager` first, then `SteamPlatform` (shutting Steam down with sockets alive crashes inside Steam).
- Dedicated servers use ENet (they have no Steam login). Steam "game server" mode is not implemented.
- Two instances on **one** Steam account can't test the relay against each other; use two accounts/machines for that.

## Testing and debugging

`cmake --build build --target net_loopback_test && (cd build && ./net_loopback_test)` runs 13 test groups: serialization (incl. hostile input),
messages, compression, handshake edge cases (version/content mismatch, bad/duplicate names, full server, game reject, timeouts, unauthenticated
senders), the full stack over ENet, Loopback and Steam, the simulator (latency, ordering under jitter, loss), flood protection, a cross-thread
loopback server, and Steam identity rules (duplicate login, bans, unreachable host). The Steam groups need the Steam client running.

Fake bad networks: `SessionOptions::simulation = {.latency_ms = 80, .jitter_ms = 20, .loss = 0.05f}` (also sliders in `net_debug_ui.hpp`).
It delays/drops what THIS process sends (loss only hits `Channel::Unreliable`), so both sides must enable it for symmetric lag.
`NetworkManager::stats()` gives bytes/packets in and out for the debug overlay.

## Not done yet

Needs game systems first: chunk streaming queue + bandwidth budget per client, entity relevance / delta-compressed snapshots, client-side
prediction and reconciliation (see `replication.hpp`, `client.hpp`). Optional later: Steam lobbies/browser, Steam auth tickets, ENet encryption,
Steam game-server mode for dedicated servers.
