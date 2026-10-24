// ENet transport backend (UDP, direct IP:port). See net/transport.hpp.

#include "core/log.hpp"
#include "net/transport_backend.hpp"

#include <enet.h>

#include <algorithm>
#include <mutex>

namespace t3d::net::detail
{
    namespace
    {
        constexpr auto CHANNEL_COUNT = static_cast<std::size_t>(Channel::Count);
        constexpr auto GRACEFUL_DISCONNECT_WAIT_MS = 300;

        // Reference-counted ENet global init/deinit, so several hosts (server + client in one process) coexist.
        std::mutex runtime_mutex;
        int runtime_users = 0;

        class EnetRuntime
        {
          public:
            EnetRuntime()
            {
                std::lock_guard lock{runtime_mutex};
                if (runtime_users == 0 && enet_initialize() != 0)
                {
                    log_error("net: enet_initialize failed");
                    return;
                }
                ++runtime_users;
                ok_ = true;
            }
            ~EnetRuntime()
            {
                if (!ok_) { return; }
                std::lock_guard lock{runtime_mutex};
                if (--runtime_users == 0) { enet_deinitialize(); }
            }
            EnetRuntime(EnetRuntime const &) = delete;
            auto operator=(EnetRuntime const &) -> EnetRuntime & = delete;
            [[nodiscard]] auto ok() const -> bool { return ok_; }

          private:
            bool ok_ = false;
        };

        auto peer_from_slot(ENetPeer const * peer) -> std::optional<PeerId>
        {
            // peer->data holds slot + 1 so that nullptr means "not registered".
            auto const value = reinterpret_cast<std::uintptr_t>(peer->data);
            if (value == 0) { return std::nullopt; }
            return static_cast<PeerId>(value - 1);
        }

        // Builds [type][payload] directly inside the ENet packet (single allocation, single copy).
        auto make_packet(PacketType type, std::span<std::byte const> payload, Channel channel) -> ENetPacket *
        {
            auto const flags = channel == Channel::Unreliable ? 0u : static_cast<enet_uint32>(ENET_PACKET_FLAG_RELIABLE);
            ENetPacket * packet = enet_packet_create(nullptr, payload.size() + 1, flags);
            if (packet == nullptr) { return nullptr; }
            packet->data[0] = static_cast<enet_uint8>(type);
            if (!payload.empty()) { std::memcpy(packet->data + 1, payload.data(), payload.size()); }
            return packet;
        }

        // Consumes the ENet packet.
        void deliver(ENetPacket * packet, PeerId sender, PacketInbox & inbox)
        {
            deliver_frame({packet->data, packet->dataLength}, sender, inbox);
            enet_packet_destroy(packet);
        }

        // =========================================================================================
        class EnetClient final : public ClientBackend
        {
          public:
            explicit EnetClient(PacketInbox & inbox) : inbox_{inbox} {}
            ~EnetClient() override { EnetClient::disconnect(); }

            auto connect(Endpoint const & endpoint, std::chrono::milliseconds timeout) -> bool override
            {
                if (state_ != ClientState::Disconnected || !runtime_.ok()) { return false; }
                error_.clear();

                ENetAddress address{};
                if (enet_address_set_host(&address, endpoint.host.c_str()) != 0)
                {
                    error_ = "could not resolve host '" + endpoint.host + "'";
                    return false;
                }
                address.port = endpoint.port;

                host_ = enet_host_create(nullptr, 1, CHANNEL_COUNT, 0, 0);
                if (host_ == nullptr)
                {
                    error_ = "could not create client socket";
                    return false;
                }
                server_ = enet_host_connect(host_, &address, CHANNEL_COUNT, 0);
                if (server_ == nullptr)
                {
                    error_ = "could not start connection";
                    reset();
                    return false;
                }
                state_ = ClientState::Connecting;
                deadline_ = std::chrono::steady_clock::now() + timeout;
                return true;
            }

            void disconnect() override
            {
                if (host_ == nullptr) { return; }
                if (state_ == ClientState::Connected)
                {
                    enet_peer_disconnect_later(server_, 0);
                    ENetEvent event;
                    while (enet_host_service(host_, &event, GRACEFUL_DISCONNECT_WAIT_MS) > 0)
                    {
                        if (event.type == ENET_EVENT_TYPE_RECEIVE) { enet_packet_destroy(event.packet); }
                        else if (event.type == ENET_EVENT_TYPE_DISCONNECT || event.type == ENET_EVENT_TYPE_DISCONNECT_TIMEOUT) { break; }
                    }
                }
                reset();
            }

            void update() override
            {
                if (host_ == nullptr) { return; }
                if (state_ == ClientState::Connecting && std::chrono::steady_clock::now() > deadline_)
                {
                    error_ = "connection timed out";
                    reset();
                    return;
                }
                ENetEvent event;
                while (host_ != nullptr && enet_host_service(host_, &event, 0) > 0)
                {
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT: state_ = ClientState::Connected; break;
                    case ENET_EVENT_TYPE_RECEIVE: deliver(event.packet, 0, inbox_); break;
                    case ENET_EVENT_TYPE_DISCONNECT:
                    case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                        error_ = state_ == ClientState::Connecting ? "connection refused" : "connection lost";
                        reset(); // already-received packets (e.g. a Kick reason) stay in the inbox
                        return;
                    default: break;
                    }
                }
            }

            [[nodiscard]] auto state() const -> ClientState override { return state_; }
            [[nodiscard]] auto last_error() const -> std::string override { return error_; }
            [[nodiscard]] auto rtt_ms() const -> std::uint32_t override { return server_ != nullptr ? server_->roundTripTime : 0; }

            void send(PacketType type, std::span<std::byte const> payload, Channel channel) override
            {
                if (state_ != ClientState::Connected) { return; }
                ENetPacket * packet = make_packet(type, payload, channel);
                if (packet != nullptr && enet_peer_send(server_, static_cast<enet_uint8>(channel), packet) < 0)
                {
                    enet_packet_destroy(packet);
                }
            }

          private:
            void reset()
            {
                if (host_ != nullptr) { enet_host_destroy(host_); }
                host_ = nullptr;
                server_ = nullptr;
                state_ = ClientState::Disconnected;
            }

            PacketInbox & inbox_;
            EnetRuntime runtime_;
            ENetHost * host_ = nullptr;
            ENetPeer * server_ = nullptr;
            ClientState state_ = ClientState::Disconnected;
            std::chrono::steady_clock::time_point deadline_{};
            std::string error_;
        };

        // =========================================================================================
        class EnetServer final : public ServerBackend
        {
          public:
            explicit EnetServer(ServerShared & shared) : shared_{shared} {}
            ~EnetServer() override { EnetServer::stop(); }

            auto start(ServerConfig const & config) -> bool override
            {
                if (host_ != nullptr || !runtime_.ok()) { return false; }
                ENetAddress address{};
                address.host = ENET_HOST_ANY;
                address.port = config.port;
                host_ = enet_host_create(&address, config.max_clients, CHANNEL_COUNT, 0, 0);
                if (host_ == nullptr)
                {
                    error_ = "could not listen on port " + std::to_string(config.port) + " (already in use?)";
                    return false;
                }
                slots_.assign(config.max_clients, nullptr);
                return true;
            }

            void stop() override
            {
                if (host_ == nullptr) { return; }
                for (ENetPeer * peer : slots_)
                {
                    if (peer != nullptr) { enet_peer_disconnect_later(peer, 0); }
                }
                // Give the disconnect notices a moment to go out; ignore anything else that arrives.
                ENetEvent event;
                while (enet_host_service(host_, &event, GRACEFUL_DISCONNECT_WAIT_MS) > 0)
                {
                    if (event.type == ENET_EVENT_TYPE_RECEIVE) { enet_packet_destroy(event.packet); }
                }
                enet_host_destroy(host_);
                host_ = nullptr;
                slots_.clear();
            }

            void update() override
            {
                if (host_ == nullptr) { return; }
                ENetEvent event;
                while (enet_host_service(host_, &event, 0) > 0)
                {
                    switch (event.type)
                    {
                    case ENET_EVENT_TYPE_CONNECT:
                    {
                        auto const free_slot = std::ranges::find(slots_, nullptr);
                        if (free_slot == slots_.end())
                        {
                            enet_peer_reset(event.peer); // cannot normally happen: ENet already limits peers to max_clients
                            break;
                        }
                        auto const id = static_cast<PeerId>(free_slot - slots_.begin());
                        *free_slot = event.peer;
                        event.peer->data = reinterpret_cast<void *>(static_cast<std::uintptr_t>(id) + 1);
                        shared_.events.push_back({.peer = id, .connected = true});
                        break;
                    }
                    case ENET_EVENT_TYPE_RECEIVE:
                    {
                        auto const sender = peer_from_slot(event.peer);
                        if (!sender) { enet_packet_destroy(event.packet); break; }
                        deliver(event.packet, *sender, shared_.inbox);
                        break;
                    }
                    case ENET_EVENT_TYPE_DISCONNECT:
                    case ENET_EVENT_TYPE_DISCONNECT_TIMEOUT:
                    {
                        auto const id = peer_from_slot(event.peer);
                        if (!id) { break; }
                        slots_[*id] = nullptr;
                        event.peer->data = nullptr;
                        shared_.events.push_back({.peer = *id, .connected = false});
                        break;
                    }
                    default: break;
                    }
                }
            }

            [[nodiscard]] auto last_error() const -> std::string override { return error_; }
            [[nodiscard]] auto client_count() const -> std::size_t override
            {
                return static_cast<std::size_t>(std::ranges::count_if(slots_, [](ENetPeer const * p) { return p != nullptr; }));
            }
            [[nodiscard]] auto is_connected(PeerId peer) const -> bool override { return peer < slots_.size() && slots_[peer] != nullptr; }
            [[nodiscard]] auto rtt_ms(PeerId peer) const -> std::uint32_t override { return is_connected(peer) ? slots_[peer]->roundTripTime : 0; }
            [[nodiscard]] auto identity(PeerId) const -> std::uint64_t override { return 0; } // ENet has no authenticated identity

            void disconnect_peer(PeerId peer) override
            {
                if (is_connected(peer)) { enet_peer_disconnect_later(slots_[peer], 0); }
            }

            void send_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel channel) override
            {
                if (!is_connected(peer)) { return; }
                ENetPacket * packet = make_packet(type, payload, channel);
                if (packet != nullptr && enet_peer_send(slots_[peer], static_cast<enet_uint8>(channel), packet) < 0)
                {
                    enet_packet_destroy(packet);
                }
            }

            void broadcast(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except) override
            {
                if (host_ == nullptr) { return; }
                ENetPacket * packet = make_packet(type, payload, channel);
                if (packet == nullptr) { return; }
                // One packet shared by all peers: ENet reference-counts it.
                for (PeerId id = 0; id < slots_.size(); ++id)
                {
                    if (slots_[id] == nullptr || id == except) { continue; }
                    enet_peer_send(slots_[id], static_cast<enet_uint8>(channel), packet);
                }
                if (packet->referenceCount == 0) { enet_packet_destroy(packet); } // nobody received it
            }

          private:
            ServerShared & shared_;
            EnetRuntime runtime_;
            ENetHost * host_ = nullptr;
            std::vector<ENetPeer *> slots_; // index = PeerId, nullptr = free
            std::string error_;
        };
    } // namespace

    auto make_enet_client(PacketInbox & inbox) -> std::unique_ptr<ClientBackend> { return std::make_unique<EnetClient>(inbox); }
    auto make_enet_server(ServerShared & shared) -> std::unique_ptr<ServerBackend> { return std::make_unique<EnetServer>(shared); }
} // namespace t3d::net::detail
