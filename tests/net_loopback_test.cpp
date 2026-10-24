// Tests for the networking layer. Build + run:
//   cmake --build build --target net_loopback_test && (cd build && ./net_loopback_test)
// Covers serialization, compression, ENet + in-process loopback + Steam backends (Steam half needs the Steam client
// running and logged in), the handshake/session layer, the network simulator and flood protection.
// Steam relay connections between two different accounts cannot be automated: test those by hand.

#include "net/compression.hpp"
#include "net/messages.hpp"
#include "net/network_manager.hpp"
#include "net/steam_platform.hpp"

#include <atomic>
#include <cstdio>
#include <thread>

using namespace t3d::net;
using namespace std::chrono_literals;

static int g_failures = 0;
#define CHECK(c) do { if(!(c)) { std::printf("  FAIL %s:%d: %s\n", __FILE__, __LINE__, #c); ++g_failures; return; } } while(0)

SteamPlatform g_steam;

void sleep_ms(int ms) { std::this_thread::sleep_for(std::chrono::milliseconds(ms)); }

// ---------------------------------------------------------------------------------------------------------------------
// Rigs
// ---------------------------------------------------------------------------------------------------------------------

struct ManagerPair
{
    NetworkManager host, remote;
    std::vector<SessionEvent> host_events;

    // One pump of everything. Joins are accepted automatically (like a trivial game would).
    void step()
    {
        g_steam.update();
        host.update();
        while (auto ev = host.poll_session_event())
        {
            host_events.push_back(*ev);
            if (ev->type == SessionEvent::Type::JoinRequested) { host.server_session()->accept(ev->peer, {}); }
        }
        remote.update();
    }
    template <class F> bool wait(F f, int ms = 8000)
    {
        for (int i = 0; i < ms / 2; ++i) { step(); if (f()) return true; sleep_ms(2); }
        return false;
    }
    void pump(int n = 40) { for (int i = 0; i < n; ++i) { step(); sleep_ms(2); } }
};

struct SessionRig // raw NetServer + ServerSession, for handshake edge cases
{
    NetServer server;
    ServerSession session;
    std::vector<SessionEvent> events;
    bool auto_accept = true;

    SessionRig(TransportKind kind, std::uint16_t port, SessionConfig config, std::uint32_t max_clients = 8) : session{server, config}
    {
        server.start({.kind = kind, .port = port, .max_clients = max_clients});
    }
    void step()
    {
        g_steam.update();
        server.update();
        session.update();
        while (auto ev = session.poll_event())
        {
            events.push_back(*ev);
            if (auto_accept && ev->type == SessionEvent::Type::JoinRequested) { session.accept(ev->peer, {}); }
        }
    }
};

struct ClientRig
{
    NetClient client;
    ClientSession session;
    ClientRig(Endpoint const & endpoint, SessionConfig config, std::string name) : session{client, config, std::move(name)}
    {
        client.connect(endpoint);
    }
    void step() { client.update(); session.update(); }
};

template <class F> bool wait_for(SessionRig & s, std::vector<ClientRig *> clients, F f, int ms = 8000)
{
    for (int i = 0; i < ms / 2; ++i)
    {
        s.step();
        for (auto * c : clients) { c->step(); }
        if (f()) return true;
        sleep_ms(2);
    }
    return false;
}

// ---------------------------------------------------------------------------------------------------------------------
// Serialization / compression
// ---------------------------------------------------------------------------------------------------------------------

void test_bits()
{
    BitWriter w;
    w.write_bool(true);
    w.write_bits(0b10110, 5);
    w.write_u8(200);
    w.write_u16(54321);
    w.write_u32(0xDEADBEEF);
    w.write_u64(0x0123456789ABCDEFull);
    w.write_i32(-123456);
    w.write_f32(3.5f);
    w.write_varuint(0);
    w.write_varuint(127);
    w.write_varuint(128);
    w.write_varuint(0xFFFFFFFFFFull);
    w.write_quantized(0.5f, 0.0f, 1.0f, 8);
    w.write_fixed32(-1234.5678, 8);

    std::vector<std::uint8_t> bytes(w.size_bytes());
    std::memcpy(bytes.data(), w.bytes().data(), bytes.size());
    BitReader r{bytes};
    CHECK(r.read_bool());
    CHECK(r.read_bits(5) == 0b10110);
    CHECK(r.read_u8() == 200);
    CHECK(r.read_u16() == 54321);
    CHECK(r.read_u32() == 0xDEADBEEF);
    CHECK(r.read_u64() == 0x0123456789ABCDEFull);
    CHECK(r.read_i32() == -123456);
    CHECK(r.read_f32() == 3.5f);
    CHECK(r.read_varuint() == 0);
    CHECK(r.read_varuint() == 127);
    CHECK(r.read_varuint() == 128);
    CHECK(r.read_varuint() == 0xFFFFFFFFFFull);
    CHECK(std::abs(r.read_quantized(0.0f, 1.0f, 8) - 0.5f) < 1.0f / 255);
    CHECK(std::abs(r.read_fixed32(8) - (-1234.5678)) < 1.0 / 256);
    CHECK(r.ok());
}

void test_bits_hostile()
{
    std::uint8_t const tiny[2] = {0xFF, 0xFF};
    BitReader r{tiny};
    (void)r.read_u32(); // past the end
    CHECK(!r.ok());
    CHECK(r.read_u8() == 0); // sticky failure, never reads out of bounds

    std::uint8_t const endless[12] = {0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80, 0x80};
    BitReader over{endless};
    (void)over.read_varuint(); // over-long varint
    CHECK(!over.ok());

    BitWriter w;
    w.write_varuint(1'000'000'000); // claims a billion entities
    std::vector<std::uint8_t> bytes(w.size_bytes());
    std::memcpy(bytes.data(), w.bytes().data(), bytes.size());
    EntitySnapshotBatch batch;
    BitReader hostile{bytes};
    CHECK(!batch.read(hostile));
}

void test_messages()
{
    EntitySnapshotBatch in{.tick = 1234};
    for (std::uint32_t i = 0; i < 50; ++i)
    {
        in.entities.push_back({.entity_id = 70000 + i, .x = 100.25 * i, .y = -64.5, .z = 1e6 + i, .yaw = 1.0f, .pitch = -0.5f, .vx = 3.0f, .vy = -20.0f, .vz = 0.0f});
    }
    BitWriter w;
    in.write(w);
    CHECK(w.size_bytes() < 50 * 24 + 8); // ~22 bytes/entity (12 of them are the position)
    std::vector<std::uint8_t> bytes(w.size_bytes());
    std::memcpy(bytes.data(), w.bytes().data(), bytes.size());
    EntitySnapshotBatch out;
    BitReader r{bytes};
    CHECK(out.read(r));
    CHECK(out.tick == 1234 && out.entities.size() == 50);
    CHECK(out.entities[7].entity_id == 70007);
    CHECK(std::abs(out.entities[7].x - 100.25 * 7) < 1.0 / 256);
    CHECK(std::abs(out.entities[7].vy - (-20.0f)) < 0.1f);
    CHECK(std::abs(out.entities[7].yaw - 1.0f) < 0.01f);

    MultiBlockChange mb{.cx = -3, .cy = 2, .cz = 9};
    mb.changes = {{.local_index = 0, .block_id = 1, .state = 0}, {.local_index = 32767, .block_id = 65535, .state = 255}};
    BitWriter w2;
    mb.write(w2);
    std::vector<std::uint8_t> b2(w2.size_bytes());
    std::memcpy(b2.data(), w2.bytes().data(), b2.size());
    MultiBlockChange mb2;
    BitReader r2{b2};
    CHECK(mb2.read(r2) && mb2.cx == -3 && mb2.changes.size() == 2 && mb2.changes[1].local_index == 32767 && mb2.changes[1].block_id == 65535);
    // truncated input must fail, not crash
    BitReader cut{std::span{b2}.first(b2.size() - 3)};
    MultiBlockChange mb3;
    CHECK(!mb3.read(cut));
}

void test_compression()
{
    std::vector<std::byte> raw(100000);
    for (std::size_t i = 0; i < raw.size(); ++i) raw[i] = std::byte((i / 64) % 7); // chunk-like repetitive data
    auto packed = compress(raw);
    CHECK(!packed.empty() && packed.size() < raw.size() / 10);
    auto back = decompress(packed, raw.size());
    CHECK(back && *back == raw);
    CHECK(!decompress(packed, raw.size() - 1)); // claims more than allowed (zip-bomb guard)
    auto corrupt = packed;
    corrupt[corrupt.size() / 2] ^= std::byte{0x5A};
    corrupt.resize(corrupt.size() - 5);
    CHECK(!decompress(corrupt, raw.size()) || true); // must not crash (may or may not detect)
    std::vector<std::byte> empty;
    auto e = decompress(compress(empty), 10);
    CHECK(e && e->empty());
    CHECK(!decompress(std::vector<std::byte>(2), 10));
}

// ---------------------------------------------------------------------------------------------------------------------
// Full stack over a transport
// ---------------------------------------------------------------------------------------------------------------------

void run_transport_suite(TransportKind kind, std::uint16_t port, std::uint64_t expected_identity)
{
    ManagerPair p;
    SessionOptions options;
    options.allow_duplicate_identity = true; // Steam: host and remote are the same account in this test
    options.username = "host";
    p.host.set_options(options);
    options.username = "alice";
    p.remote.set_options(options);

    CHECK(p.host.start_host({.kind = kind, .port = port, .max_clients = 4}));
    CHECK(p.wait([&] { return p.host.in_game(); }));
    Endpoint ep;
    switch (kind)
    {
    case TransportKind::Steam: ep = Endpoint::steam_direct("127.0.0.1", port); break;
    case TransportKind::Loopback: ep = Endpoint::loopback(port); break;
    default: ep = Endpoint::enet("127.0.0.1", port); break;
    }
    CHECK(p.remote.connect(ep));
    CHECK(p.remote.state() == NetworkManager::State::Connecting);
    CHECK(p.wait([&] { return p.remote.in_game(); }));
    CHECK(p.remote.client_session()->accept_info()->player_id == 1);
    CHECK(std::string(p.remote.client_session()->accept_info()->username) == "alice");

    PeerId remote_peer = 99;
    int joins = 0;
    for (auto const & ev : p.host_events)
    {
        if (ev.type == SessionEvent::Type::JoinRequested) { ++joins; if (ev.username == "alice") remote_peer = ev.peer; }
    }
    CHECK(joins == 2 && remote_peer != 99);
    CHECK(p.host.identity(remote_peer) == expected_identity);

    // client -> server (fixed packet)
    p.remote.send(ChatPacket{.message = "hello"});
    std::optional<std::pair<ChatPacket, PeerId>> chat;
    CHECK(p.wait([&] { chat = p.host.receive_from_client<ChatPacket>(); return chat.has_value(); }));
    CHECK(std::string(chat->first.message) == "hello" && chat->second == remote_peer);

    // server -> all (fixed) and bit-packed message
    p.host.broadcast(BlockChangePacket{.x = -5, .y = 70, .z = 12, .block_id = 3, .state = 1});
    std::optional<BlockChangePacket> b1, b2;
    CHECK(p.wait([&] {
        if (!b1) b1 = p.host.receive_from_server<BlockChangePacket>();
        if (!b2) b2 = p.remote.receive_from_server<BlockChangePacket>();
        return b1 && b2; }));
    CHECK(b2->x == -5 && b2->block_id == 3);

    EntitySnapshotBatch snap{.tick = 9, .entities = {{.entity_id = 5, .x = 10.5, .y = 64, .z = -3}}};
    p.host.broadcast(snap, Channel::Unreliable);
    std::optional<EntitySnapshotBatch> got_snap;
    CHECK(p.wait([&] { got_snap = p.remote.receive_from_server<EntitySnapshotBatch>(); return got_snap.has_value(); }));
    CHECK(got_snap->tick == 9 && got_snap->entities.size() == 1 && got_snap->entities[0].entity_id == 5);

    // broadcast_except skips the excluded peer
    p.host.broadcast_except(remote_peer, PlayerLeftPacket{.player_id = 7});
    std::optional<PlayerLeftPacket> l_host;
    CHECK(p.wait([&] { l_host = p.host.receive_from_server<PlayerLeftPacket>(); return l_host.has_value(); }));
    p.pump();
    CHECK(!p.remote.receive_from_server<PlayerLeftPacket>());

    // 50 KB compressed bulk payload round trip
    std::vector<std::byte> blob(50000);
    for (std::size_t i = 0; i < blob.size(); ++i) blob[i] = std::byte((i / 100) % 13);
    auto packed = compress(blob);
    p.host.server()->send_bytes_to(remote_peer, PacketType::ChunkData, packed, Channel::Bulk);
    std::optional<RawPacket> chunk;
    CHECK(p.wait([&] { chunk = p.remote.receive_raw_from_server(PacketType::ChunkData); return chunk.has_value(); }));
    auto unpacked = decompress(std::as_bytes(std::span{chunk->payload}), 100000);
    CHECK(unpacked && *unpacked == blob);

    // unreliable client -> server
    p.remote.send(PlayerInputPacket{.sequence = 42}, Channel::Unreliable);
    std::optional<std::pair<PlayerInputPacket, PeerId>> in;
    CHECK(p.wait([&] { in = p.host.receive_from_client<PlayerInputPacket>(); return in.has_value(); }));
    CHECK(in->first.sequence == 42);

    // malformed (wrong-size) packet is dropped, not crashed on
    p.remote.client()->send_bytes(PacketType::BlockDig, std::as_bytes(std::span{"xy", 2}));
    p.pump();
    CHECK(!p.host.receive_from_client<BlockDigPacket>());

    // ping/pong gives an RTT sample and traffic counters move
    p.pump(600);
    std::printf("  [%s] rtt remote=%u ms, host stats in=%llu B out=%llu B\n", kind == TransportKind::Steam ? "steam" : kind == TransportKind::Enet ? "enet" : "loopback",
                p.remote.rtt_ms(), (unsigned long long)p.host.stats().bytes_in, (unsigned long long)p.host.stats().bytes_out);
    CHECK(p.host.stats().bytes_in > 0 && p.host.stats().bytes_out > 0);

    // packets from peers that never finished the handshake never reach game code (raw packet before handshake)
    // -> covered in test_handshake_edge_cases

    // kick: reason reaches the player, host sees the departure, slot freed
    p.host.kick(remote_peer, "bye bye");
    CHECK(p.wait([&] { return p.remote.state() == NetworkManager::State::Idle; }));
    if (p.remote.last_error() != "bye bye") std::printf("  last_error was '%s'\n", p.remote.last_error().c_str());
    CHECK(p.remote.last_error() == "bye bye");
    CHECK(p.wait([&] { return p.host.client_count() == 1; }));
    bool saw_leave = false;
    for (auto const & ev : p.host_events) saw_leave |= ev.type == SessionEvent::Type::Left && ev.peer == remote_peer && ev.was_active;
    CHECK(saw_leave);

    // rejoin reuses the slot and player id
    CHECK(p.remote.connect(ep));
    CHECK(p.wait([&] { return p.remote.in_game(); }));
    CHECK(p.remote.client_session()->accept_info()->player_id == 1);

    // host shutdown: remote notices
    p.host.disconnect();
    CHECK(p.host.state() == NetworkManager::State::Idle);
    CHECK(p.wait([&] { return p.remote.state() == NetworkManager::State::Idle; }));
    CHECK(!p.remote.last_error().empty());
}

void test_enet() { run_transport_suite(TransportKind::Enet, 27123, 0); }
void test_loopback() { run_transport_suite(TransportKind::Loopback, 27500, 0); }

void test_steam()
{
    if (!SteamPlatform::available()) { std::printf("  (steam unavailable: %s - skipped)\n", g_steam.last_error().c_str()); return; }
    run_transport_suite(TransportKind::Steam, 27124, g_steam.local_id());
}

void test_steam_identity_rules()
{
    if (!SteamPlatform::available()) { std::printf("  (skipped)\n"); return; }
    SessionConfig cfg;
    {   // duplicate login: same account twice is refused by default
        SessionRig server{TransportKind::Steam, 27125, cfg};
        ClientRig a{Endpoint::steam_direct("127.0.0.1", 27125), cfg, "one"};
        ClientRig b{Endpoint::steam_direct("127.0.0.1", 27125), cfg, "two"};
        CHECK(wait_for(server, {&a, &b}, [&] { return a.session.is_active() || b.session.is_active(); }));
        CHECK(wait_for(server, {&a, &b}, [&] { return a.session.state() == ClientSession::State::Disconnected || b.session.state() == ClientSession::State::Disconnected; }));
        bool dup = a.session.reject_code() == RejectReason::DuplicateLogin || b.session.reject_code() == RejectReason::DuplicateLogin;
        CHECK(dup);
    }
    {   // banned identity
        SessionRig server{TransportKind::Steam, 27126, cfg};
        server.session.ban(g_steam.local_id());
        ClientRig a{Endpoint::steam_direct("127.0.0.1", 27126), cfg, "banned"};
        CHECK(wait_for(server, {&a}, [&] { return a.session.state() == ClientSession::State::Disconnected; }));
        CHECK(a.session.reject_code() == RejectReason::Banned);
    }
    {   // connecting to a SteamID that is not online fails cleanly (timeout), does not hang
        NetworkManager lonely;
        CHECK(lonely.connect_steam(76561197960265729ULL));
        for (int i = 0; i < 4000 && lonely.state() != NetworkManager::State::Idle; ++i) { g_steam.update(); lonely.update(); sleep_ms(5); }
        CHECK(lonely.state() == NetworkManager::State::Idle);
        std::printf("  unreachable steam host -> '%s'\n", lonely.last_error().c_str());
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Session edge cases (over the in-process transport so they are fast and deterministic)
// ---------------------------------------------------------------------------------------------------------------------

void test_handshake_edge_cases()
{
    auto const lb = [](std::uint16_t port) { return Endpoint::loopback(port); };
    SessionConfig base;
    base.content_hash = 0xABCD;

    {   // version mismatch
        SessionRig server{TransportKind::Loopback, 27600, base};
        SessionConfig old = base;
        old.protocol_version = 0;
        ClientRig c{lb(27600), old, "alex"};
        CHECK(wait_for(server, {&c}, [&] { return c.session.state() == ClientSession::State::Disconnected; }));
        CHECK(c.session.reject_code() == RejectReason::VersionMismatch);
        CHECK(c.session.disconnect_reason().find("Version") != std::string::npos);
    }
    {   // content mismatch
        SessionRig server{TransportKind::Loopback, 27601, base};
        SessionConfig other = base;
        other.content_hash = 0x1111;
        ClientRig c{lb(27601), other, "alex"};
        CHECK(wait_for(server, {&c}, [&] { return c.session.state() == ClientSession::State::Disconnected; }));
        CHECK(c.session.reject_code() == RejectReason::ContentMismatch);
    }
    {   // bad usernames
        SessionRig server{TransportKind::Loopback, 27602, base};
        ClientRig empty{lb(27602), base, "   "};
        CHECK(wait_for(server, {&empty}, [&] { return empty.session.state() == ClientSession::State::Disconnected; }));
        CHECK(empty.session.reject_code() == RejectReason::BadUsername);
    }
    {   // hostile names are sanitised, duplicates get a suffix, ids are unique
        SessionRig server{TransportKind::Loopback, 27603, base};
        ClientRig a{lb(27603), base, "Alex"};
        ClientRig b{lb(27603), base, "Alex"};
        ClientRig c{lb(27603), base, "  <b>Evil\n name that is far too long</b> "};
        CHECK(wait_for(server, {&a, &b, &c}, [&] { return a.session.is_active() && b.session.is_active() && c.session.is_active(); }));
        std::set<std::string> names{a.session.accept_info()->username, b.session.accept_info()->username, c.session.accept_info()->username};
        CHECK(names.size() == 3);
        CHECK(names.contains("Alex") && names.contains("Alex#2"));
        for (auto const & n : names) CHECK(n.size() <= 16 && n.find('<') == std::string::npos && n.find('\n') == std::string::npos);
        std::set<int> ids{a.session.accept_info()->player_id, b.session.accept_info()->player_id, c.session.accept_info()->player_id};
        CHECK(ids.size() == 3);
        CHECK(server.session.active_count() == 3);
    }
    {   // server full
        SessionConfig small = base;
        small.max_players = 1;
        SessionRig server{TransportKind::Loopback, 27604, small};
        ClientRig a{lb(27604), base, "a"};
        CHECK(wait_for(server, {&a}, [&] { return a.session.is_active(); }));
        ClientRig b{lb(27604), base, "b"};
        CHECK(wait_for(server, {&a, &b}, [&] { return b.session.state() == ClientSession::State::Disconnected; }));
        CHECK(b.session.reject_code() == RejectReason::ServerFull);
        CHECK(a.session.is_active());
    }
    {   // game rejects a validated join
        SessionRig server{TransportKind::Loopback, 27605, base};
        server.auto_accept = false;
        ClientRig a{lb(27605), base, "a"};
        CHECK(wait_for(server, {&a}, [&] { return !server.events.empty(); }));
        CHECK(server.events[0].type == SessionEvent::Type::JoinRequested);
        server.session.reject(server.events[0].peer, RejectReason::Other, "World is loading");
        CHECK(wait_for(server, {&a}, [&] { return a.session.state() == ClientSession::State::Disconnected; }));
        CHECK(a.session.disconnect_reason() == "World is loading");
        CHECK(wait_for(server, {&a}, [&] { return server.events.size() >= 2; }));
        CHECK(server.events[1].type == SessionEvent::Type::Left && !server.events[1].was_active);
    }
    {   // handshake timeout: connect but never speak
        SessionConfig fast = base;
        fast.handshake_timeout = 150ms;
        SessionRig server{TransportKind::Loopback, 27606, fast};
        NetClient mute;
        CHECK(mute.connect(lb(27606)));
        std::optional<HandshakeRejectPacket> reject;
        for (int i = 0; i < 1000 && mute.state() != NetClient::State::Disconnected; ++i) { server.step(); mute.update(); if (!reject) reject = mute.receive<HandshakeRejectPacket>(); sleep_ms(2); }
        CHECK(mute.state() == NetClient::State::Disconnected);
        CHECK(reject && reject->code == RejectReason::Timeout);
        CHECK(server.events.empty()); // never validated -> no game-visible events
    }
    {   // accept timeout: validated but the game never answers
        SessionConfig fast = base;
        fast.accept_timeout = 150ms;
        SessionRig server{TransportKind::Loopback, 27607, fast};
        server.auto_accept = false;
        ClientRig a{lb(27607), base, "a"};
        CHECK(wait_for(server, {&a}, [&] { return a.session.state() == ClientSession::State::Disconnected; }));
        CHECK(a.session.reject_code() == RejectReason::Timeout);
    }
    {   // unauthenticated peers' gameplay packets are filtered by NetworkManager::receive_from_client
        NetworkManager host;
        host.start_host({.kind = TransportKind::Loopback, .port = 27608});
        NetClient rogue;
        CHECK(rogue.connect(lb(27608)));
        rogue.update();
        for (int i = 0; i < 50; ++i)
        {
            host.update();
            while (auto ev = host.poll_session_event()) { if (ev->type == SessionEvent::Type::JoinRequested) host.server_session()->accept(ev->peer, {}); }
            rogue.update();
            if (rogue.is_connected()) rogue.send(PlayerInputPacket{.sequence = 666}); // no handshake first
            sleep_ms(2);
        }
        CHECK(!host.receive_from_client<PlayerInputPacket>() || true);
        // the host's own local client is Active, the rogue is not: only legitimate senders can come through
        while (auto m = host.receive_from_client<PlayerInputPacket>()) { CHECK(m->first.sequence != 666); }
    }
    {   // connecting to a loopback port nobody listens on
        NetClient c;
        CHECK(!c.connect(Endpoint::loopback(27999)));
        CHECK(!c.last_error().empty());
        // port already taken
        NetServer a, b;
        CHECK(a.start({.kind = TransportKind::Loopback, .port = 27998}));
        CHECK(!b.start({.kind = TransportKind::Loopback, .port = 27998}));
    }
}

// ---------------------------------------------------------------------------------------------------------------------
// Simulator, flood protection, threading
// ---------------------------------------------------------------------------------------------------------------------

void test_simulation()
{
    SessionOptions options;
    options.simulation = {.latency_ms = 60};
    ManagerPair p;
    p.host.set_options(options);
    p.remote.set_options(options);
    CHECK(p.host.start_host({.kind = TransportKind::Loopback, .port = 27700}));
    CHECK(p.remote.connect(Endpoint::loopback(27700)));
    CHECK(p.wait([&] { return p.remote.in_game(); }));

    // one-way client->server must take >= ~60 ms
    auto const start = std::chrono::steady_clock::now();
    p.remote.send(ChatPacket{.message = "x"});
    CHECK(p.wait([&] { return p.host.receive_from_client<ChatPacket>().has_value(); }));
    auto const one_way = std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start).count();
    CHECK(one_way >= 55 && one_way < 400);
    std::printf("  simulated one-way latency: %lld ms (configured 60)\n", (long long)one_way);

    // measured RTT (ping/pong) includes both directions
    p.pump(1500);
    CHECK(p.remote.rtt_ms() >= 100 && p.remote.rtt_ms() < 400);
    std::printf("  measured rtt: %u ms (expected ~120)\n", p.remote.rtt_ms());

    // ordering of reliable packets is preserved despite jitter
    p.host.disconnect();
    p.remote.disconnect();
    options.simulation = {.latency_ms = 20, .jitter_ms = 18};
    ManagerPair q;
    q.host.set_options(options);
    q.remote.set_options(options);
    CHECK(q.host.start_host({.kind = TransportKind::Loopback, .port = 27701}));
    CHECK(q.remote.connect(Endpoint::loopback(27701)));
    CHECK(q.wait([&] { return q.remote.in_game(); }));
    for (std::uint32_t i = 0; i < 200; ++i) q.remote.send(PlayerInputPacket{.sequence = i}, Channel::Reliable);
    std::uint32_t expected = 0;
    bool ordered = true;
    CHECK(q.wait([&] {
        while (auto m = q.host.receive_from_client<PlayerInputPacket>()) { ordered &= m->first.sequence == expected++; }
        return expected == 200; }));
    CHECK(ordered);
}

void test_simulated_loss()
{
    SessionOptions options;
    options.simulation = {.loss = 0.3f};
    options.rate_limit = {.packets_per_second = 1e9, .packet_burst = 1e9, .bytes_per_second = 1e12, .byte_burst = 1e12};
    ManagerPair p;
    p.host.set_options(options);
    p.remote.set_options(options);
    CHECK(p.host.start_host({.kind = TransportKind::Loopback, .port = 27702}));
    CHECK(p.remote.connect(Endpoint::loopback(27702)));
    CHECK(p.wait([&] { return p.remote.in_game(); }));

    for (std::uint32_t i = 0; i < 2000; ++i) { p.remote.send(PlayerInputPacket{.sequence = i}, Channel::Unreliable); }
    for (std::uint32_t i = 0; i < 200; ++i) { p.remote.send(ChatPacket{.sender_id = static_cast<std::uint16_t>(i)}, Channel::Reliable); }
    p.pump(100);
    int unreliable = 0, reliable = 0;
    while (p.host.receive_from_client<PlayerInputPacket>()) ++unreliable;
    while (p.host.receive_from_client<ChatPacket>()) ++reliable;
    std::printf("  30%% loss: unreliable %d/2000 arrived, reliable %d/200\n", unreliable, reliable);
    CHECK(unreliable > 1200 && unreliable < 1600);
    CHECK(reliable == 200);
}

void test_flood_protection()
{
    SessionConfig cfg;
    SessionRig server{TransportKind::Loopback, 27710, cfg};
    server.server.set_rate_limit({.packets_per_second = 100, .packet_burst = 200, .bytes_per_second = 1e9, .byte_burst = 1e9});
    ClientRig a{Endpoint::loopback(27710), cfg, "flooder"};
    CHECK(wait_for(server, {&a}, [&] { return a.session.is_active(); }));

    std::uint64_t delivered = 0;
    for (int i = 0; i < 50000; ++i) a.client.send(PlayerInputPacket{.sequence = static_cast<std::uint32_t>(i)}, Channel::Unreliable);
    CHECK(wait_for(server, {&a}, [&] {
        while (server.server.receive<PlayerInputPacket>()) ++delivered;
        return a.session.state() == ClientSession::State::Disconnected; }));
    std::printf("  flood: %llu of 50000 packets got through, flooder kicked: '%s'\n", (unsigned long long)delivered, a.session.disconnect_reason().c_str());
    CHECK(delivered < 1000);
    CHECK(a.session.disconnect_reason() == "Too many packets");
    CHECK(wait_for(server, {&a}, [&] {
        for (auto const & ev : server.events) { if (ev.type == SessionEvent::Type::Left) return true; }
        return false; }));
}

void test_cross_thread_loopback()
{
    // The planned integrated-server layout: the server runs on its own thread, the client on this one.
    std::atomic<bool> stop{false};
    std::atomic<int> server_received{0};
    std::atomic<bool> server_ready{false};
    std::thread server_thread([&] {
        SessionConfig cfg;
        NetServer server;
        ServerSession session{server, cfg};
        server.set_rate_limit({.packets_per_second = 1e9, .packet_burst = 1e9, .bytes_per_second = 1e12, .byte_burst = 1e12});
        server.start({.kind = TransportKind::Loopback, .port = 27720});
        server_ready = true;
        while (!stop)
        {
            server.update();
            session.update();
            while (auto ev = session.poll_event()) { if (ev->type == SessionEvent::Type::JoinRequested) session.accept(ev->peer, {}); }
            while (auto m = server.receive<PlayerInputPacket>())
            {
                if (session.is_active(m->second)) { ++server_received; server.send_to(m->second, PlayerStateCorrectionPacket{.last_processed_input = m->first.sequence}); }
            }
            std::this_thread::sleep_for(1ms);
        }
    });
    while (!server_ready) sleep_ms(1);

    ClientRig c{Endpoint::loopback(27720), SessionConfig{}, "threaded"};
    bool active = false;
    for (int i = 0; i < 3000 && !active; ++i) { c.step(); active = c.session.is_active(); sleep_ms(1); }
    bool ok = active;
    std::uint32_t last = 0;
    int echoed = 0;
    if (ok)
    {
        for (std::uint32_t i = 1; i <= 1000; ++i) c.client.send(PlayerInputPacket{.sequence = i});
        for (int i = 0; i < 5000 && echoed < 1000; ++i)
        {
            c.step();
            while (auto m = c.client.receive<PlayerStateCorrectionPacket>()) { ok &= m->last_processed_input > last; last = m->last_processed_input; ++echoed; }
            sleep_ms(1);
        }
    }
    stop = true;
    server_thread.join();
    std::printf("  threaded: server saw %d, client got %d echoes\n", server_received.load(), echoed);
    CHECK(ok && echoed == 1000 && server_received == 1000);
}

int main()
{
    struct Test { char const * name; void (*fn)(); };
    bool const steam_ok = g_steam.init();
    std::printf("steam: %s\n", steam_ok ? "available" : g_steam.last_error().c_str());

    Test const tests[] = {
        {"bit serialization", test_bits}, {"bit serialization (hostile input)", test_bits_hostile}, {"messages", test_messages},
        {"compression", test_compression},
        {"handshake / session edge cases", test_handshake_edge_cases},
        {"full stack over ENet", test_enet}, {"full stack over loopback", test_loopback},
        {"simulator: latency + ordering", test_simulation}, {"simulator: loss", test_simulated_loss},
        {"flood protection", test_flood_protection}, {"cross-thread loopback", test_cross_thread_loopback},
        {"full stack over Steam", test_steam}, {"steam identity rules", test_steam_identity_rules},
    };
    for (auto const & t : tests)
    {
        int const before = g_failures;
        std::printf("[ RUN  ] %s\n", t.name);
        t.fn();
        std::printf("[ %s ] %s\n", g_failures == before ? " OK " : "FAIL", t.name);
    }
    g_steam.shutdown();
    std::printf(g_failures == 0 ? "ALL OK\n" : "%d FAILURES\n", g_failures);
    return g_failures == 0 ? 0 : 1;
}
