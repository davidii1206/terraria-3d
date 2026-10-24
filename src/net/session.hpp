#pragma once
// Session layer: everything between "a transport connection exists" and "this is a player in the game".
//
// SERVER (ServerSession, one per NetServer)
//   connect -> Handshaking (must send HandshakePacket within handshake_timeout)
//           -> validated: protocol version, content hash, ban list, duplicate login, capacity, username
//              => emits SessionEvent::JoinRequested, peer is Validated and has a PlayerId
//           -> the GAME decides: accept(peer, info) -> Active   |   reject(peer, ...) / kick(peer, ...)
//   Also handled here so game code never sees it: Ping -> Pong, handshake/accept timeouts,
//   kicking peers that flood (rate-limit violations), ban list, PlayerId allocation.
//   Left events are emitted for every peer that reached Validated, whatever the reason.
//   While a ServerSession is attached, do NOT poll NetServer::poll_connection_event() yourself.
//   Game code must ignore packets from peers that are not Active (NetworkManager::receive_from_client does).
//
// CLIENT (ClientSession, one per NetClient)
//   Connecting (transport) -> Handshaking (HandshakePacket sent) -> Active (HandshakeAcceptPacket)
//   Any failure ends in Disconnected with a human readable disconnect_reason(): server reject text,
//   kick reason, or the transport error. Also measures round-trip time with Ping/Pong.
//
// Usernames: the client's name is treated as hostile text. The server trims it, replaces anything that is not
// [A-Za-z0-9 _-] with '_', limits it to 16 chars, rejects empty names and makes it unique ("Alex", "Alex#2").
// Identity (bans, saves, permissions) must use the transport-verified id (NetServer::identity), never the name.

#include "core/types.hpp"
#include "net/transport.hpp"

#include <chrono>
#include <deque>
#include <optional>
#include <set>
#include <string>
#include <string_view>
#include <vector>

namespace t3d::net
{
    struct SessionConfig
    {
        std::uint32_t protocol_version = PROTOCOL_VERSION;
        std::uint64_t content_hash = 0;     // must match between server and clients (hash of loaded data/ registries)
        std::uint32_t max_players = 0;      // 0 = only the transport's connection limit applies
        std::uint32_t tick_rate = SERVER_TICK_RATE;
        std::chrono::milliseconds handshake_timeout{5000}; // from connect until the Handshake packet arrives
        std::chrono::milliseconds accept_timeout{15000};   // from validation until the game calls accept()
        std::uint32_t max_rate_violations_per_second = 100; // kick above this many rate-limited packets per second
        bool allow_duplicate_identity = false; // true only for tests (one Steam account playing against itself)
    };

    struct SessionEvent
    {
        enum class Type { JoinRequested, Left };
        Type type = Type::JoinRequested;
        PeerId peer = 0;
        PlayerId player_id = 0;
        std::uint64_t identity = 0; // transport-verified (SteamID64), 0 for ENet/loopback
        std::string username;       // sanitised and unique
        bool was_active = false;    // Left only: false if the player left before the game accepted them
    };

    class ServerSession
    {
      public:
        ServerSession(NetServer & server, SessionConfig config);

        // Call once per tick, right after NetServer::update().
        void update();
        [[nodiscard]] auto poll_event() -> std::optional<SessionEvent>;

        // Admit a validated peer. Fills player_id / tick_rate / username into `info` and sends it.
        void accept(PeerId peer, HandshakeAcceptPacket info);
        // Refuse a validated or handshaking peer with a reason shown to the player.
        void reject(PeerId peer, RejectReason code, std::string_view text);
        // Remove an active player with a reason (sends KickPacket first).
        void kick(PeerId peer, std::string_view reason);

        void ban(std::uint64_t identity) { if (identity != 0) { banned_.insert(identity); } }
        void unban(std::uint64_t identity) { banned_.erase(identity); }
        [[nodiscard]] auto is_banned(std::uint64_t identity) const -> bool { return banned_.contains(identity); }
        [[nodiscard]] auto banned() const -> std::set<std::uint64_t> const & { return banned_; }
        void set_banned(std::set<std::uint64_t> ids) { banned_ = std::move(ids); } // load from disk

        [[nodiscard]] auto is_active(PeerId peer) const -> bool;
        [[nodiscard]] auto player_id(PeerId peer) const -> std::optional<PlayerId>;
        [[nodiscard]] auto peer_of(PlayerId player) const -> std::optional<PeerId>;
        [[nodiscard]] auto username(PeerId peer) const -> std::string;
        [[nodiscard]] auto active_count() const -> std::size_t;
        // Packets from not-yet-active peers that were dropped by the caller (diagnostics).
        [[nodiscard]] auto config() const -> SessionConfig const & { return config_; }

      private:
        enum class State { Handshaking, Validated, Active, Closing };
        struct Peer
        {
            State state = State::Handshaking;
            std::chrono::steady_clock::time_point deadline;
            PlayerId player_id = 0;
            std::uint64_t identity = 0;
            std::string username;
            bool joined = false;       // reached Validated: the game knows about this peer, so it must get a Left event
            bool was_active = false;   // the game accepted it
            std::uint32_t violations = 0;
            std::chrono::steady_clock::time_point violation_window_start;
        };

        void on_connected(PeerId peer);
        void on_disconnected(PeerId peer);
        void handle_handshake(PeerId peer, HandshakePacket const & packet);
        void send_reject(PeerId peer, RejectReason code, std::string_view text);
        auto make_unique_username(std::string name) const -> std::string;
        auto free_player_id() const -> std::optional<PlayerId>;
        auto peer_at(PeerId peer) -> Peer *;
        auto peer_at(PeerId peer) const -> Peer const *;

        NetServer & server_;
        SessionConfig config_;
        std::vector<std::optional<Peer>> peers_;
        std::deque<SessionEvent> events_;
        std::set<std::uint64_t> banned_;
    };

    class ClientSession
    {
      public:
        enum class State { Disconnected, Connecting, Handshaking, Active };

        ClientSession(NetClient & client, SessionConfig config, std::string username);

        // Call once per frame, right after NetClient::update().
        void update();

        [[nodiscard]] auto state() const -> State { return state_; }
        [[nodiscard]] auto is_active() const -> bool { return state_ == State::Active; }
        // Valid once Active.
        [[nodiscard]] auto accept_info() const -> std::optional<HandshakeAcceptPacket> const & { return accept_; }
        // Why the session ended / failed (server reject text, kick reason or transport error). Empty if never failed.
        [[nodiscard]] auto disconnect_reason() const -> std::string const & { return reason_; }
        [[nodiscard]] auto reject_code() const -> std::optional<RejectReason> { return reject_code_; }
        // Smoothed round-trip time from Ping/Pong in ms (0 until the first Pong arrives).
        [[nodiscard]] auto rtt_ms() const -> std::uint32_t { return static_cast<std::uint32_t>(rtt_ms_); }

      private:
        auto now_ms() const -> std::uint32_t;

        NetClient & client_;
        SessionConfig config_;
        std::string username_;
        State state_ = State::Connecting;
        std::optional<HandshakeAcceptPacket> accept_;
        std::optional<RejectReason> reject_code_;
        std::string reason_;
        double rtt_ms_ = 0;
        std::chrono::steady_clock::time_point start_;
        std::chrono::steady_clock::time_point handshake_deadline_;
        std::chrono::steady_clock::time_point next_ping_;
    };
} // namespace t3d::net
