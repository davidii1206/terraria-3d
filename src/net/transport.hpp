#pragma once
// Transport layer: typed, poll-based client/server endpoints on top of interchangeable backends.
//
// BACKENDS (selected at runtime by TransportKind)
//   Enet   UDP via ENet. Direct IP:port. Used for dedicated servers, LAN, tests, and as the fallback when Steam is absent.
//   Steam  ISteamNetworkingSockets. Friends connect to the host's SteamID through Valve's relay network, so
//          nobody needs port forwarding. Topology is still CLIENT <-> HOST SERVER (not peer-to-peer gameplay):
//          the host is the authoritative server, "P2P" here only describes how the connection is routed.
//
// The rest of the game only sees NetServer / NetClient / PacketInbox; backends live in net/enet_backend.cpp and
// net/steam_backend.cpp.
//
// DESIGN
//  * Single-threaded and poll-based: update() services the socket and fills the inbox, receive<T>() pops decoded
//    packets. No background threads, no locks, no callbacks. A NetServer or NetClient must only be touched by the
//    thread that created it (the integrated server thread owns its NetServer, the main thread its NetClient).
//  * Channels: Reliable (ordered; handshake, chat, edits), Unreliable (inputs, snapshots - stale data is dropped),
//    Bulk (reliable on its own lane; chunk streaming, so it never blocks gameplay traffic).
//  * Incoming data is untrusted: oversized/unknown/malformed packets are dropped (with a log line).
//  * Peers are addressed by PeerId = slot index; slots are reused after a disconnect. Always handle a peer's
//    disconnect event before assuming a new connect event with the same id is a new player.
//  * Identity: NetServer::identity(peer) returns the transport-verified id (SteamID64 on Steam, 0 on ENet).
//    Never trust identity claims inside packets - use this.
//
// Not included yet (see README): compression, bandwidth limits, simulated latency/loss, in-process loopback.

#include "net/transport_backend.hpp"

#include <memory>
#include <string>

namespace t3d::net
{
    // =============================================================================================
    class NetClient
    {
      public:
        using State = ClientState;

        NetClient();
        ~NetClient();
        NetClient(NetClient const &) = delete;
        auto operator=(NetClient const &) -> NetClient & = delete;

        // Non-blocking: returns true if the attempt started. Watch state() / last_error() afterwards.
        auto connect(Endpoint const & endpoint, std::chrono::milliseconds timeout = std::chrono::seconds{5}) -> bool;
        // Graceful: tells the server, waits briefly for the ack, then tears down.
        void disconnect();
        // Pump the socket. Call once per frame (or per client tick).
        void update();

        [[nodiscard]] auto state() const -> State;
        [[nodiscard]] auto is_connected() const -> bool { return state() == State::Connected; }
        [[nodiscard]] auto last_error() const -> std::string;
        [[nodiscard]] auto rtt_ms() const -> std::uint32_t;

        template <WirePacket T>
        void send(T const & packet, Channel channel = Channel::Reliable)
        {
            send_bytes(T::TYPE, std::as_bytes(std::span{&packet, 1}), channel);
        }
        template <BitMessage T>
        void send(T const & message, Channel channel = Channel::Reliable)
        {
            BitWriter writer;
            message.write(writer);
            send_bytes(T::TYPE, writer.bytes(), channel);
        }
        void send_bytes(PacketType type, std::span<std::byte const> payload, Channel channel = Channel::Reliable);

        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        [[nodiscard]] auto receive() -> std::optional<T> { return inbox_.pop<T>(); }
        [[nodiscard]] auto receive_raw(PacketType type) -> std::optional<RawPacket> { return inbox_.pop_raw(type); }

        // Fake latency/jitter/loss for everything this client sends. Takes effect on the next connect().
        void set_simulation(NetSimConfig const & config) { simulation_ = config; }
        [[nodiscard]] auto stats() const -> TrafficStats const & { return inbox_.stats(); }

      private:
        PacketInbox inbox_;
        NetSimConfig simulation_;
        std::unique_ptr<detail::ClientBackend> backend_;
        std::string error_; // errors that happen before a backend exists
    };

    // =============================================================================================
    class NetServer
    {
      public:
        NetServer();
        ~NetServer();
        NetServer(NetServer const &) = delete;
        auto operator=(NetServer const &) -> NetServer & = delete;

        auto start(ServerConfig const & config) -> bool;
        // Disconnects everyone (gracefully, short wait) and closes the sockets.
        void stop();
        void update();

        [[nodiscard]] auto is_running() const -> bool;
        [[nodiscard]] auto last_error() const -> std::string const & { return error_; }
        [[nodiscard]] auto client_count() const -> std::size_t;
        [[nodiscard]] auto is_connected(PeerId peer) const -> bool;
        [[nodiscard]] auto rtt_ms(PeerId peer) const -> std::uint32_t;
        // Transport-verified identity (SteamID64 on Steam, 0 for ENet). Valid while the peer is connected.
        [[nodiscard]] auto identity(PeerId peer) const -> std::uint64_t;

        // Disconnect one peer (kick). Send a KickPacket first if the player should see a reason.
        void disconnect_peer(PeerId peer);

        template <WirePacket T>
        void send_to(PeerId peer, T const & packet, Channel channel = Channel::Reliable)
        {
            send_bytes_to(peer, T::TYPE, std::as_bytes(std::span{&packet, 1}), channel);
        }
        template <WirePacket T>
        void broadcast(T const & packet, Channel channel = Channel::Reliable)
        {
            broadcast_bytes(T::TYPE, std::as_bytes(std::span{&packet, 1}), channel, std::nullopt);
        }
        template <WirePacket T>
        void broadcast_except(PeerId except, T const & packet, Channel channel = Channel::Reliable)
        {
            broadcast_bytes(T::TYPE, std::as_bytes(std::span{&packet, 1}), channel, except);
        }
        template <BitMessage T>
        void send_to(PeerId peer, T const & message, Channel channel = Channel::Reliable)
        {
            BitWriter writer;
            message.write(writer);
            send_bytes_to(peer, T::TYPE, writer.bytes(), channel);
        }
        template <BitMessage T>
        void broadcast(T const & message, Channel channel = Channel::Reliable)
        {
            BitWriter writer;
            message.write(writer);
            broadcast_bytes(T::TYPE, writer.bytes(), channel, std::nullopt);
        }
        template <BitMessage T>
        void broadcast_except(PeerId except, T const & message, Channel channel = Channel::Reliable)
        {
            BitWriter writer;
            message.write(writer);
            broadcast_bytes(T::TYPE, writer.bytes(), channel, except);
        }
        void send_bytes_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel channel = Channel::Reliable);
        void broadcast_bytes(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except);

        // Packets sent by clients. `sender` identifies the peer.
        template <typename T>
            requires(WirePacket<T> || BitMessage<T>)
        [[nodiscard]] auto receive() -> std::optional<std::pair<T, PeerId>>
        {
            PeerId sender = 0;
            if (auto value = inbox().template pop<T>(&sender)) { return std::pair{*value, sender}; }
            return std::nullopt;
        }
        [[nodiscard]] auto receive_raw(PacketType type) -> std::optional<RawPacket> { return inbox().pop_raw(type); }

        // Connect/disconnect notifications, in order.
        [[nodiscard]] auto poll_connection_event() -> std::optional<ConnectionEvent>;

        // Fake latency/jitter/loss for everything the server sends. Takes effect on the next start().
        void set_simulation(NetSimConfig const & config) { simulation_ = config; }
        // Per-peer flood protection (on by default with RateLimit{} defaults).
        void set_rate_limit(RateLimit const & limit) { limit_ = limit; }
        // Packets dropped from this peer by the rate limiter since the last call (resets to 0).
        [[nodiscard]] auto take_rate_violations(PeerId peer) -> std::uint32_t;
        [[nodiscard]] auto stats() const -> TrafficStats const &;

      private:
        auto inbox() -> PacketInbox &;
        auto admit(PeerId peer, std::size_t bytes) -> bool;

        struct Bucket
        {
            double packets = 0, bytes = 0;
            std::chrono::steady_clock::time_point last{};
            std::uint32_t violations = 0;
            bool fresh = true;
        };
        std::vector<Bucket> buckets_;
        RateLimit limit_;
        NetSimConfig simulation_;

        std::unique_ptr<detail::ServerShared> shared_;
        std::unique_ptr<detail::ServerBackend> backend_;
        std::string error_;
    };
} // namespace t3d::net
