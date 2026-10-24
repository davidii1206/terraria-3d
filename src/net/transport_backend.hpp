#pragma once
// Internal interfaces implemented by each transport backend (ENet, Steam). Game code never includes this.
// Backends receive raw frames and hand them to the shared inbox via deliver_frame(); they are single-threaded,
// like the facades that own them.

#include "net/transport_types.hpp"

#include <chrono>
#include <deque>
#include <memory>
#include <span>
#include <string>

namespace t3d::net::detail
{
    // State shared between NetServer and its backend.
    struct ServerShared
    {
        PacketInbox inbox;
        std::deque<ConnectionEvent> events;
    };

    class ClientBackend
    {
      public:
        virtual ~ClientBackend() = default;
        virtual auto connect(Endpoint const & endpoint, std::chrono::milliseconds timeout) -> bool = 0;
        virtual void disconnect() = 0;
        virtual void update() = 0;
        [[nodiscard]] virtual auto state() const -> ClientState = 0;
        [[nodiscard]] virtual auto last_error() const -> std::string = 0;
        [[nodiscard]] virtual auto rtt_ms() const -> std::uint32_t = 0;
        virtual void send(PacketType type, std::span<std::byte const> payload, Channel channel) = 0;
    };

    class ServerBackend
    {
      public:
        virtual ~ServerBackend() = default;
        virtual auto start(ServerConfig const & config) -> bool = 0;
        virtual void stop() = 0;
        virtual void update() = 0;
        [[nodiscard]] virtual auto last_error() const -> std::string = 0;
        [[nodiscard]] virtual auto client_count() const -> std::size_t = 0;
        [[nodiscard]] virtual auto is_connected(PeerId peer) const -> bool = 0;
        [[nodiscard]] virtual auto rtt_ms(PeerId peer) const -> std::uint32_t = 0;
        [[nodiscard]] virtual auto identity(PeerId peer) const -> std::uint64_t = 0;
        virtual void disconnect_peer(PeerId peer) = 0;
        virtual void send_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel channel) = 0;
        virtual void broadcast(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except) = 0;
    };

    // Validates a received frame ([type][payload]) and queues it. Frames that are empty, oversized or of unknown
    // type are dropped and logged.
    void deliver_frame(std::span<std::uint8_t const> frame, PeerId sender, PacketInbox & inbox);

    // Factories. The Steam ones return nullptr (and set `error`) when built without Steamworks.
    auto make_enet_client(PacketInbox & inbox) -> std::unique_ptr<ClientBackend>;
    auto make_enet_server(ServerShared & shared) -> std::unique_ptr<ServerBackend>;
    auto make_loopback_client(PacketInbox & inbox, std::string & error) -> std::unique_ptr<ClientBackend>;
    auto make_loopback_server(ServerShared & shared) -> std::unique_ptr<ServerBackend>;
    // Wraps a backend so everything it sends is delayed / dropped according to `config`.
    auto make_simulated(std::unique_ptr<ClientBackend> inner, NetSimConfig const & config) -> std::unique_ptr<ClientBackend>;
    auto make_simulated(std::unique_ptr<ServerBackend> inner, NetSimConfig const & config) -> std::unique_ptr<ServerBackend>;
    auto make_steam_client(PacketInbox & inbox, std::string & error) -> std::unique_ptr<ClientBackend>;
    auto make_steam_server(ServerShared & shared, std::string & error) -> std::unique_ptr<ServerBackend>;
} // namespace t3d::net::detail
