// Network condition simulator: decorates any backend and delays / drops what it SENDS.
// See NetSimConfig in net/transport_types.hpp.

#include "net/transport_backend.hpp"

#include <algorithm>
#include <functional>
#include <random>

namespace t3d::net::detail
{
    namespace
    {
        using Clock = std::chrono::steady_clock;

        // Delivery queue shared by the client and server decorators.
        class DelayLine
        {
          public:
            explicit DelayLine(NetSimConfig const & config) : config_{config}, rng_{std::random_device{}()} {}

            // Queues `action` for later; returns without queueing if the packet is "lost".
            void submit(Channel channel, std::function<void()> action)
            {
                if (channel == Channel::Unreliable && config_.loss > 0.0f && uniform_(rng_) < config_.loss) { return; }
                auto delay = static_cast<std::int64_t>(config_.latency_ms);
                if (config_.jitter_ms != 0)
                {
                    delay += static_cast<std::int64_t>(uniform_(rng_) * 2.0 * config_.jitter_ms) - static_cast<std::int64_t>(config_.jitter_ms);
                }
                auto release = Clock::now() + std::chrono::milliseconds{std::max<std::int64_t>(delay, 0)};
                // Reliable/ordered traffic must not overtake itself, jitter or not. Unreliable may reorder.
                if (channel != Channel::Unreliable)
                {
                    release = std::max(release, last_ordered_release_);
                    last_ordered_release_ = release;
                }
                queue_.push_back({release, std::move(action)});
            }

            void flush_due()
            {
                auto const now = Clock::now();
                // Stable partition keeps submission order among items that are due.
                for (auto it = queue_.begin(); it != queue_.end();)
                {
                    if (it->release <= now)
                    {
                        auto action = std::move(it->action);
                        it = queue_.erase(it);
                        action();
                    }
                    else { ++it; }
                }
            }

            void flush_all()
            {
                for (auto & item : queue_) { item.action(); }
                queue_.clear();
            }

          private:
            struct Item
            {
                Clock::time_point release;
                std::function<void()> action;
            };
            NetSimConfig config_;
            std::mt19937 rng_;
            std::uniform_real_distribution<float> uniform_{0.0f, 1.0f};
            std::deque<Item> queue_;
            Clock::time_point last_ordered_release_{};
        };

        auto copy_payload(std::span<std::byte const> payload) { return std::vector<std::byte>(payload.begin(), payload.end()); }

        class SimClient final : public ClientBackend
        {
          public:
            SimClient(std::unique_ptr<ClientBackend> inner, NetSimConfig const & config) : inner_{std::move(inner)}, line_{config} {}
            ~SimClient() override { SimClient::disconnect(); }

            auto connect(Endpoint const & endpoint, std::chrono::milliseconds timeout) -> bool override { return inner_->connect(endpoint, timeout); }
            void disconnect() override
            {
                line_.flush_all(); // let queued packets out before closing
                inner_->disconnect();
            }
            void update() override
            {
                line_.flush_due();
                inner_->update();
            }
            [[nodiscard]] auto state() const -> ClientState override { return inner_->state(); }
            [[nodiscard]] auto last_error() const -> std::string override { return inner_->last_error(); }
            [[nodiscard]] auto rtt_ms() const -> std::uint32_t override { return inner_->rtt_ms(); }
            void send(PacketType type, std::span<std::byte const> payload, Channel channel) override
            {
                line_.submit(channel, [this, type, data = copy_payload(payload), channel] { inner_->send(type, data, channel); });
            }

          private:
            std::unique_ptr<ClientBackend> inner_;
            DelayLine line_;
        };

        class SimServer final : public ServerBackend
        {
          public:
            SimServer(std::unique_ptr<ServerBackend> inner, NetSimConfig const & config) : inner_{std::move(inner)}, line_{config} {}
            ~SimServer() override { SimServer::stop(); }

            auto start(ServerConfig const & config) -> bool override { return inner_->start(config); }
            void stop() override
            {
                line_.flush_all();
                inner_->stop();
            }
            void update() override
            {
                line_.flush_due();
                inner_->update();
            }
            [[nodiscard]] auto last_error() const -> std::string override { return inner_->last_error(); }
            [[nodiscard]] auto client_count() const -> std::size_t override { return inner_->client_count(); }
            [[nodiscard]] auto is_connected(PeerId peer) const -> bool override { return inner_->is_connected(peer); }
            [[nodiscard]] auto rtt_ms(PeerId peer) const -> std::uint32_t override { return inner_->rtt_ms(peer); }
            [[nodiscard]] auto identity(PeerId peer) const -> std::uint64_t override { return inner_->identity(peer); }
            void disconnect_peer(PeerId peer) override
            {
                // Kick notices queued before the disconnect must still go out first.
                line_.submit(Channel::Reliable, [this, peer] { inner_->disconnect_peer(peer); });
            }
            void send_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel channel) override
            {
                line_.submit(channel, [this, peer, type, data = copy_payload(payload), channel] { inner_->send_to(peer, type, data, channel); });
            }
            void broadcast(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except) override
            {
                line_.submit(channel, [this, type, data = copy_payload(payload), channel, except] { inner_->broadcast(type, data, channel, except); });
            }

          private:
            std::unique_ptr<ServerBackend> inner_;
            DelayLine line_;
        };
    } // namespace

    auto make_simulated(std::unique_ptr<ClientBackend> inner, NetSimConfig const & config) -> std::unique_ptr<ClientBackend>
    {
        return std::make_unique<SimClient>(std::move(inner), config);
    }
    auto make_simulated(std::unique_ptr<ServerBackend> inner, NetSimConfig const & config) -> std::unique_ptr<ServerBackend>
    {
        return std::make_unique<SimServer>(std::move(inner), config);
    }
} // namespace t3d::net::detail
