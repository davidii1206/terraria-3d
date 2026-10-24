#pragma once
// NetworkManager: the one object the rest of the game talks to.
//
// Owns an optional NetServer + ServerSession (hosting) and/or NetClient + ClientSession (playing) and exposes a
// small, typed, poll-based API - no callbacks:
//
//     net.update();                                                    // once per frame / tick
//     while (auto ev = net.poll_session_event()) { ... }               // server: JoinRequested -> accept()/reject(), Left
//     while (auto m = net.receive_from_client<PlayerInputPacket>()) { auto [pkt, peer] = *m; ... }   // server side
//     while (auto p = net.receive_from_server<BlockChangePacket>()) { ... }                           // client side
//
// MODES
//   Idle        nothing running
//   Hosting     server + a local client connected to it (singleplayer / "host game"), over Steam-loopback, ENet
//               loopback or the in-process Loopback transport, depending on ServerConfig::kind
//   Connecting  client reaching a remote server (non-blocking; watch state())
//   Connected   client connected to a remote server
// "Connected" only means the TRANSPORT is up. The player is in the game when in_game() is true (handshake accepted).
//
// THREADING: single-threaded. Call everything from the thread that created the manager. For the planned
// "integrated server on its own thread" setup, give that thread its own NetServer + ServerSession
// (net/transport.hpp, net/session.hpp) over TransportKind::Loopback and keep this class for the client side.
//
// STEAM: SteamPlatform (net/steam_platform.hpp) must be initialised and pumped by the app, and must outlive the manager.

#include "net/session.hpp"
#include "net/transport.hpp"

#include <memory>
#include <string>

namespace t3d::net
{
    // Everything the handshake needs; set before start_host()/connect().
    struct SessionOptions
    {
        std::string username = "Player";
        std::uint64_t content_hash = 0;  // hash of the data/ registries; server and clients must agree
        NetSimConfig simulation{};       // fake latency/loss for this process's traffic (testing)
        RateLimit rate_limit{};          // server side flood protection
        bool allow_duplicate_identity = false; // tests only: same Steam account may join twice
    };

    class NetworkManager
    {
      public:
        enum class State { Idle, Hosting, Connecting, Connected };

        NetworkManager();
        ~NetworkManager();
        NetworkManager(NetworkManager const &) = delete;
        auto operator=(NetworkManager const &) -> NetworkManager & = delete;

        void set_options(SessionOptions options) { options_ = std::move(options); }
        [[nodiscard]] auto options() const -> SessionOptions const & { return options_; }

        // Starts a server plus a local client connected to it. With TransportKind::Steam, friends can join through
        // the relay by SteamID (see SteamPlatform::local_id / publish_join_info); with Enet they need your IP:port;
        // with Loopback only clients in this process can connect (singleplayer).
        auto start_host(ServerConfig const & config = {}) -> bool;
        // Starts connecting to a server. Returns false only if the attempt could not even start;
        // later failures (refused, timeout, rejected) flip state() back to Idle and set last_error().
        auto connect(Endpoint const & endpoint) -> bool;
        auto connect(std::string const & host, std::uint16_t port = 25565) -> bool { return connect(Endpoint::enet(host, port)); }
        auto connect_steam(std::uint64_t host_steam_id) -> bool { return connect(Endpoint::steam_relay(host_steam_id)); }
        void disconnect();
        // Pumps sockets and sessions. Call once per frame (client) / per tick (server).
        void update();

        [[nodiscard]] auto state() const -> State;
        [[nodiscard]] auto is_hosting() const -> bool { return server_ != nullptr; }
        [[nodiscard]] auto is_active() const -> bool { return state() != State::Idle; }
        // Transport level: the local client is talking to a server.
        [[nodiscard]] auto is_connected() const -> bool { return client_ != nullptr && client_->is_connected(); }
        // Session level: the server accepted us; gameplay packets may flow.
        [[nodiscard]] auto in_game() const -> bool { return client_session_ != nullptr && client_session_->is_active(); }
        [[nodiscard]] auto last_error() const -> std::string const & { return last_error_; }
        [[nodiscard]] auto rtt_ms() const -> std::uint32_t;
        [[nodiscard]] auto client_count() const -> std::size_t { return server_ != nullptr ? server_->client_count() : 0; }
        [[nodiscard]] auto stats() const -> TrafficStats; // this process: client + server traffic summed
        // Transport-verified id of a connected player (SteamID64, or 0 on ENet/loopback). Use for bans/saves, never packet fields.
        [[nodiscard]] auto identity(PeerId peer) const -> std::uint64_t { return server_ != nullptr ? server_->identity(peer) : 0; }

        // --- Client side -------------------------------------------------------------------------
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        void send(T const & packet, Channel channel = Channel::Reliable)
        {
            if (client_ != nullptr) { client_->send(packet, channel); }
        }
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        [[nodiscard]] auto receive_from_server() -> std::optional<T>
        {
            return client_ != nullptr ? client_->receive<T>() : std::nullopt;
        }
        [[nodiscard]] auto receive_raw_from_server(PacketType type) -> std::optional<RawPacket>
        {
            return client_ != nullptr ? client_->receive_raw(type) : std::nullopt;
        }
        [[nodiscard]] auto client_session() -> ClientSession * { return client_session_.get(); }

        // --- Server side (no-ops unless hosting) -------------------------------------------------
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        void send_to_client(PeerId peer, T const & packet, Channel channel = Channel::Reliable)
        {
            if (server_ != nullptr) { server_->send_to(peer, packet, channel); }
        }
        // Broadcasts reach every peer that is connected, including ones still handshaking. Use send_to_active
        // helpers (below) for gameplay traffic.
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        void broadcast(T const & packet, Channel channel = Channel::Reliable)
        {
            if (server_ != nullptr) { server_->broadcast(packet, channel); }
        }
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        void broadcast_except(PeerId except, T const & packet, Channel channel = Channel::Reliable)
        {
            if (server_ != nullptr) { server_->broadcast_except(except, packet, channel); }
        }
        // Packets from peers that completed the handshake. Packets from anyone else are dropped.
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        [[nodiscard]] auto receive_from_client() -> std::optional<std::pair<T, PeerId>>
        {
            if (server_ == nullptr) { return std::nullopt; }
            while (auto message = server_->receive<T>())
            {
                if (server_session_->is_active(message->second)) { return message; }
            }
            return std::nullopt;
        }
        [[nodiscard]] auto receive_raw_from_client(PacketType type) -> std::optional<RawPacket>
        {
            if (server_ == nullptr) { return std::nullopt; }
            while (auto raw = server_->receive_raw(type))
            {
                if (server_session_->is_active(raw->sender)) { return raw; }
            }
            return std::nullopt;
        }
        // Join requests (validated, waiting for ServerSession::accept/reject) and departures.
        [[nodiscard]] auto poll_session_event() -> std::optional<SessionEvent>
        {
            return server_session_ != nullptr ? server_session_->poll_event() : std::nullopt;
        }
        void kick(PeerId peer, std::string_view reason) { if (server_session_ != nullptr) { server_session_->kick(peer, reason); } }
        [[nodiscard]] auto server_session() -> ServerSession * { return server_session_.get(); }

        // Escape hatches for code that needs the raw transport objects (null when not in that role).
        [[nodiscard]] auto server() -> NetServer * { return server_.get(); }
        [[nodiscard]] auto client() -> NetClient * { return client_.get(); }

      private:
        auto session_config(std::uint32_t max_players) const -> SessionConfig;

        SessionOptions options_;
        std::unique_ptr<NetServer> server_;
        std::unique_ptr<ServerSession> server_session_;
        std::unique_ptr<NetClient> client_;
        std::unique_ptr<ClientSession> client_session_;
        std::string last_error_;
    };
} // namespace t3d::net
