// In-process transport (no sockets, no Steam, no ENet). Used for singleplayer and tests.
//
// A NetServer started with TransportKind::Loopback registers itself under its "port" number in a process-wide
// hub; NetClients connect with Endpoint::loopback(port). Frames travel through mutex-protected queues, so the
// server and the client may live on different threads (the planned integrated-server thread). Delivery is
// instantaneous and lossless; combine with NetSimConfig to test latency behaviour offline.

#include "core/log.hpp"
#include "net/transport_backend.hpp"

#include <algorithm>
#include <memory>
#include <mutex>
#include <unordered_map>

namespace t3d::net::detail
{
    namespace
    {
        using Frame = std::vector<std::uint8_t>;
        constexpr std::size_t MAX_QUEUED_FRAMES = 1 << 16; // a stalled peer must not grow the queue forever

        auto make_frame(PacketType type, std::span<std::byte const> payload) -> Frame
        {
            Frame frame(payload.size() + 1);
            frame[0] = static_cast<std::uint8_t>(type);
            if (!payload.empty()) { std::memcpy(frame.data() + 1, payload.data(), payload.size()); }
            return frame;
        }

        // One client <-> server connection.
        struct Link
        {
            std::mutex mutex;
            std::deque<Frame> to_server;
            std::deque<Frame> to_client;
            bool accepted = false;
            bool closed_by_server = false;
            bool closed_by_client = false;
        };

        struct Endpoint_ // a listening server
        {
            std::mutex mutex;
            std::vector<std::shared_ptr<Link>> pending;
            bool open = true;
        };

        struct Hub
        {
            std::mutex mutex;
            std::unordered_map<std::uint16_t, std::shared_ptr<Endpoint_>> servers;
        };

        auto hub() -> Hub &
        {
            static Hub instance;
            return instance;
        }

        // =========================================================================================
        class LoopbackClient final : public ClientBackend
        {
          public:
            explicit LoopbackClient(PacketInbox & inbox) : inbox_{inbox} {}
            ~LoopbackClient() override { LoopbackClient::disconnect(); }

            auto connect(Endpoint const & endpoint, std::chrono::milliseconds timeout) -> bool override
            {
                if (state_ != ClientState::Disconnected) { return false; }
                error_.clear();

                std::shared_ptr<Endpoint_> server;
                {
                    std::lock_guard lock{hub().mutex};
                    if (auto const it = hub().servers.find(endpoint.port); it != hub().servers.end()) { server = it->second; }
                }
                if (server == nullptr)
                {
                    error_ = "no loopback server on port " + std::to_string(endpoint.port);
                    return false;
                }
                link_ = std::make_shared<Link>();
                {
                    std::lock_guard lock{server->mutex};
                    if (!server->open)
                    {
                        error_ = "loopback server is closing";
                        link_.reset();
                        return false;
                    }
                    server->pending.push_back(link_);
                }
                state_ = ClientState::Connecting;
                deadline_ = std::chrono::steady_clock::now() + timeout;
                return true;
            }

            void disconnect() override
            {
                if (link_ != nullptr)
                {
                    std::lock_guard lock{link_->mutex};
                    link_->closed_by_client = true;
                }
                link_.reset();
                state_ = ClientState::Disconnected;
            }

            void update() override
            {
                if (link_ == nullptr) { return; }
                std::deque<Frame> frames;
                bool server_gone;
                {
                    std::lock_guard lock{link_->mutex};
                    if (link_->accepted && state_ == ClientState::Connecting) { state_ = ClientState::Connected; }
                    frames.swap(link_->to_client);
                    server_gone = link_->closed_by_server;
                }
                for (auto const & frame : frames) { deliver_frame(frame, 0, inbox_); }

                if (server_gone) // everything the server sent before closing (e.g. a Kick) was delivered above
                {
                    error_ = state_ == ClientState::Connecting ? "connection refused" : "server closed the connection";
                    link_.reset();
                    state_ = ClientState::Disconnected;
                }
                else if (state_ == ClientState::Connecting && std::chrono::steady_clock::now() > deadline_)
                {
                    error_ = "connection timed out";
                    disconnect();
                }
            }

            [[nodiscard]] auto state() const -> ClientState override { return state_; }
            [[nodiscard]] auto last_error() const -> std::string override { return error_; }
            [[nodiscard]] auto rtt_ms() const -> std::uint32_t override { return 0; }

            void send(PacketType type, std::span<std::byte const> payload, Channel) override
            {
                if (state_ != ClientState::Connected) { return; }
                std::lock_guard lock{link_->mutex};
                if (link_->to_server.size() < MAX_QUEUED_FRAMES) { link_->to_server.push_back(make_frame(type, payload)); }
            }

          private:
            PacketInbox & inbox_;
            std::shared_ptr<Link> link_;
            ClientState state_ = ClientState::Disconnected;
            std::chrono::steady_clock::time_point deadline_{};
            std::string error_;
        };

        // =========================================================================================
        class LoopbackServer final : public ServerBackend
        {
          public:
            explicit LoopbackServer(ServerShared & shared) : shared_{shared} {}
            ~LoopbackServer() override { LoopbackServer::stop(); }

            auto start(ServerConfig const & config) -> bool override
            {
                if (endpoint_ != nullptr) { return false; }
                auto endpoint = std::make_shared<Endpoint_>();
                {
                    std::lock_guard lock{hub().mutex};
                    if (!hub().servers.try_emplace(config.port, endpoint).second)
                    {
                        error_ = "loopback port " + std::to_string(config.port) + " already in use";
                        return false;
                    }
                }
                endpoint_ = std::move(endpoint);
                port_ = config.port;
                slots_.assign(config.max_clients, nullptr);
                return true;
            }

            void stop() override
            {
                if (endpoint_ == nullptr) { return; }
                {
                    std::lock_guard lock{hub().mutex};
                    hub().servers.erase(port_);
                }
                {
                    std::lock_guard lock{endpoint_->mutex};
                    endpoint_->open = false;
                    for (auto const & link : endpoint_->pending) { close_link(*link); }
                    endpoint_->pending.clear();
                }
                for (auto & link : slots_)
                {
                    if (link != nullptr) { close_link(*link); }
                }
                slots_.clear();
                endpoint_.reset();
            }

            void update() override
            {
                if (endpoint_ == nullptr) { return; }
                accept_pending();
                for (PeerId id = 0; id < slots_.size(); ++id)
                {
                    if (slots_[id] == nullptr) { continue; }
                    std::deque<Frame> frames;
                    bool client_gone;
                    {
                        std::lock_guard lock{slots_[id]->mutex};
                        frames.swap(slots_[id]->to_server);
                        client_gone = slots_[id]->closed_by_client;
                    }
                    for (auto const & frame : frames) { deliver_frame(frame, id, shared_.inbox); }
                    if (client_gone) { release(id); }
                }
            }

            [[nodiscard]] auto last_error() const -> std::string override { return error_; }
            [[nodiscard]] auto client_count() const -> std::size_t override
            {
                return static_cast<std::size_t>(std::ranges::count_if(slots_, [](auto const & l) { return l != nullptr; }));
            }
            [[nodiscard]] auto is_connected(PeerId peer) const -> bool override { return peer < slots_.size() && slots_[peer] != nullptr; }
            [[nodiscard]] auto rtt_ms(PeerId) const -> std::uint32_t override { return 0; }
            [[nodiscard]] auto identity(PeerId) const -> std::uint64_t override { return 0; }

            void disconnect_peer(PeerId peer) override
            {
                if (!is_connected(peer)) { return; }
                close_link(*slots_[peer]);
                release(peer);
            }

            void send_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel) override
            {
                if (!is_connected(peer)) { return; }
                std::lock_guard lock{slots_[peer]->mutex};
                if (slots_[peer]->to_client.size() < MAX_QUEUED_FRAMES) { slots_[peer]->to_client.push_back(make_frame(type, payload)); }
            }

            void broadcast(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except) override
            {
                for (PeerId id = 0; id < slots_.size(); ++id)
                {
                    if (slots_[id] != nullptr && id != except) { send_to(id, type, payload, channel); }
                }
            }

          private:
            static void close_link(Link & link)
            {
                std::lock_guard lock{link.mutex};
                link.closed_by_server = true;
            }

            void accept_pending()
            {
                std::vector<std::shared_ptr<Link>> incoming;
                {
                    std::lock_guard lock{endpoint_->mutex};
                    incoming.swap(endpoint_->pending);
                }
                for (auto & link : incoming)
                {
                    auto const free_slot = std::ranges::find(slots_, nullptr);
                    if (free_slot == slots_.end())
                    {
                        close_link(*link); // server full: the client sees "connection refused"
                        continue;
                    }
                    {
                        std::lock_guard lock{link->mutex};
                        link->accepted = true;
                    }
                    *free_slot = std::move(link);
                    shared_.events.push_back({.peer = static_cast<PeerId>(free_slot - slots_.begin()), .connected = true});
                }
            }

            void release(PeerId id)
            {
                slots_[id].reset();
                shared_.events.push_back({.peer = id, .connected = false});
            }

            ServerShared & shared_;
            std::shared_ptr<Endpoint_> endpoint_;
            std::uint16_t port_ = 0;
            std::vector<std::shared_ptr<Link>> slots_;
            std::string error_;
        };
    } // namespace

    auto make_loopback_client(PacketInbox & inbox, std::string &) -> std::unique_ptr<ClientBackend> { return std::make_unique<LoopbackClient>(inbox); }
    auto make_loopback_server(ServerShared & shared) -> std::unique_ptr<ServerBackend> { return std::make_unique<LoopbackServer>(shared); }
} // namespace t3d::net::detail
