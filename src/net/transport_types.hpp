#pragma once
// Plain types shared by the transport facade (transport.hpp) and its backends (transport_backend.hpp).

#include "net/messages.hpp"

#include <array>
#include <chrono>
#include <cstdint>
#include <cstring>
#include <deque>
#include <functional>
#include <memory>
#include <optional>
#include <span>
#include <string>
#include <utility>
#include <vector>

namespace t3d::net
{
    using PeerId = std::uint32_t;

    enum class TransportKind : std::uint8_t { Enet, Steam, Loopback };
    enum class Channel : std::uint8_t { Reliable = 0, Unreliable = 1, Bulk = 2, Count };

    inline constexpr std::size_t MAX_INCOMING_PACKET_SIZE = 64 * 1024; // larger incoming packets are dropped
    inline constexpr std::size_t MAX_INBOX_BYTES = 32 * 1024 * 1024;   // inbox flood protection (all types together)

    // Where a client connects to.
    struct Endpoint
    {
        TransportKind kind = TransportKind::Enet;
        std::string host = "127.0.0.1"; // Enet, or Steam via direct IP (the host's own local client)
        std::uint16_t port = 25565;
        std::uint64_t steam_id = 0;     // Steam: connect to this SteamID through the relay (host != 0)

        static auto enet(std::string host, std::uint16_t port) -> Endpoint
        {
            return {.kind = TransportKind::Enet, .host = std::move(host), .port = port};
        }
        static auto steam_relay(std::uint64_t steam_id) -> Endpoint
        {
            return {.kind = TransportKind::Steam, .steam_id = steam_id};
        }
        // In-process connection to a NetServer with TransportKind::Loopback and the same port number (no sockets).
        static auto loopback(std::uint16_t port) -> Endpoint
        {
            return {.kind = TransportKind::Loopback, .port = port};
        }
        // Steam sockets over a direct address. Used by the host's own client (loopback).
        static auto steam_direct(std::string host, std::uint16_t port) -> Endpoint
        {
            return {.kind = TransportKind::Steam, .host = std::move(host), .port = port};
        }
    };

    struct ServerConfig
    {
        TransportKind kind = TransportKind::Enet;
        // Enet: UDP port to listen on (all interfaces).
        // Steam: friends reach the host through the relay (no port). The port is a loopback-only listener
        //        for the host's own client.
        // Loopback: in-process "port" name; clients in the same process (any thread) connect with Endpoint::loopback.
        std::uint16_t port = 25565;
        std::uint32_t max_clients = 32;
    };

    // Fake network conditions for testing prediction/interpolation locally. Applied to everything THIS endpoint sends,
    // so the round trip sees roughly 2x latency when both ends enable it (or 1x one-way if only one does).
    struct NetSimConfig
    {
        std::uint32_t latency_ms = 0;
        std::uint32_t jitter_ms = 0; // +- uniform
        float loss = 0.0f;           // 0..1, only applied to Channel::Unreliable (reliable stays reliable)
        [[nodiscard]] auto enabled() const -> bool { return latency_ms != 0 || jitter_ms != 0 || loss > 0.0f; }
    };

    // Server-side flood protection per peer: token buckets for packets and bytes. Excess packets are dropped and
    // counted (NetServer::take_rate_violations); ServerSession kicks peers that keep violating.
    struct RateLimit
    {
        double packets_per_second = 500;
        double packet_burst = 1000;
        double bytes_per_second = 256 * 1024;
        double byte_burst = 1024 * 1024;
    };

    struct TrafficStats
    {
        std::uint64_t packets_in = 0, bytes_in = 0, packets_out = 0, bytes_out = 0;
    };

    struct ConnectionEvent
    {
        PeerId peer = 0;
        bool connected = false; // false = disconnected / timed out
    };

    // A received packet with its type byte stripped.
    struct RawPacket
    {
        PeerId sender = 0; // always 0 on the client side (the server)
        std::vector<std::uint8_t> payload;
    };

    // Per-type queues of received packets. Shared by NetServer and NetClient.
    class PacketInbox
    {
      public:
        void push(PacketType type, RawPacket packet);
        [[nodiscard]] auto pop_raw(PacketType type) -> std::optional<RawPacket>;

        // Decodes a fixed-size packet; malformed ones (wrong size) are dropped and logged.
        template <WirePacket T>
        [[nodiscard]] auto pop(PeerId * sender = nullptr) -> std::optional<T>
        {
            while (auto raw = pop_raw(T::TYPE))
            {
                if (raw->payload.size() != sizeof(T)) { log_malformed(T::TYPE, raw->payload.size(), sizeof(T)); continue; }
                T value;
                std::memcpy(&value, raw->payload.data(), sizeof(T));
                if (sender != nullptr) { *sender = raw->sender; }
                return value;
            }
            return std::nullopt;
        }

        // BitMessage variant: decode failures are dropped and logged, like malformed fixed packets.
        template <BitMessage T>
        [[nodiscard]] auto pop(PeerId * sender = nullptr) -> std::optional<T>
        {
            while (auto raw = pop_raw(T::TYPE))
            {
                BitReader reader{raw->payload};
                T value;
                if (!value.read(reader)) { log_malformed(T::TYPE, raw->payload.size(), 0); continue; }
                if (sender != nullptr) { *sender = raw->sender; }
                return value;
            }
            return std::nullopt;
        }

        // Called before a received packet is queued (server: rate limiting). Return false to drop it.
        void set_admission(std::function<bool(PeerId, std::size_t)> hook) { admission_ = std::move(hook); }
        [[nodiscard]] auto admit(PeerId sender, std::size_t bytes) -> bool { return !admission_ || admission_(sender, bytes); }
        [[nodiscard]] auto stats() const -> TrafficStats const & { return stats_; }
        void count_sent(std::size_t packets, std::size_t bytes) { stats_.packets_out += packets; stats_.bytes_out += bytes; }

        void clear();

      private:
        static void log_malformed(PacketType type, std::size_t got, std::size_t expected);
        std::array<std::deque<RawPacket>, static_cast<std::size_t>(PacketType::Count)> queues_;
        std::size_t queued_bytes_ = 0;
        std::function<bool(PeerId, std::size_t)> admission_;
        TrafficStats stats_;
    };

    enum class ClientState { Disconnected, Connecting, Connected };
} // namespace t3d::net
