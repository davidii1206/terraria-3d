#include "net/transport.hpp"

#include "core/log.hpp"

#include <algorithm>

namespace t3d::net
{
    // --- PacketInbox ------------------------------------------------------------------------------

    void PacketInbox::push(PacketType type, RawPacket packet)
    {
        auto const index = static_cast<std::size_t>(type);
        if (index == 0 || index >= queues_.size())
        {
            log_warn("net: dropped packet with unknown type {}", index);
            return;
        }
        if (queued_bytes_ + packet.payload.size() > MAX_INBOX_BYTES)
        {
            log_warn("net: inbox full, dropped packet of type {}", index);
            return;
        }
        queued_bytes_ += packet.payload.size();
        ++stats_.packets_in;
        stats_.bytes_in += packet.payload.size() + 1;
        queues_[index].push_back(std::move(packet));
    }

    auto PacketInbox::pop_raw(PacketType type) -> std::optional<RawPacket>
    {
        auto const index = static_cast<std::size_t>(type);
        if (index >= queues_.size() || queues_[index].empty()) { return std::nullopt; }
        RawPacket packet = std::move(queues_[index].front());
        queues_[index].pop_front();
        queued_bytes_ -= packet.payload.size();
        return packet;
    }

    void PacketInbox::clear()
    {
        for (auto & queue : queues_) { queue.clear(); }
        queued_bytes_ = 0;
    }

    void PacketInbox::log_malformed(PacketType type, std::size_t got, std::size_t expected)
    {
        log_warn("net: dropped malformed packet type {} (payload {} bytes, expected {})", static_cast<int>(type), got, expected);
    }

    void detail::deliver_frame(std::span<std::uint8_t const> frame, PeerId sender, PacketInbox & inbox)
    {
        if (frame.empty() || frame.size() > MAX_INCOMING_PACKET_SIZE)
        {
            log_warn("net: dropped packet with invalid size {}", frame.size());
            return;
        }
        if (!inbox.admit(sender, frame.size())) { return; } // rate limited
        inbox.push(static_cast<PacketType>(frame[0]), RawPacket{.sender = sender, .payload = {frame.begin() + 1, frame.end()}});
    }

    // --- NetClient --------------------------------------------------------------------------------

    NetClient::NetClient() = default;
    NetClient::~NetClient() { disconnect(); }

    auto NetClient::connect(Endpoint const & endpoint, std::chrono::milliseconds timeout) -> bool
    {
        if (state() != State::Disconnected) { return false; }
        error_.clear();
        inbox_.clear();
        backend_.reset();

        switch (endpoint.kind)
        {
        case TransportKind::Enet: backend_ = detail::make_enet_client(inbox_); break;
        case TransportKind::Steam: backend_ = detail::make_steam_client(inbox_, error_); break;
        case TransportKind::Loopback: backend_ = detail::make_loopback_client(inbox_, error_); break;
        }
        if (backend_ == nullptr)
        {
            if (error_.empty()) { error_ = "unknown transport"; }
            return false;
        }
        if (simulation_.enabled()) { backend_ = detail::make_simulated(std::move(backend_), simulation_); }
        return backend_->connect(endpoint, timeout);
    }

    void NetClient::disconnect()
    {
        if (backend_ != nullptr) { backend_->disconnect(); } // object kept so last_error() stays readable
    }

    void NetClient::update()
    {
        if (backend_ != nullptr) { backend_->update(); }
    }

    auto NetClient::state() const -> State { return backend_ != nullptr ? backend_->state() : State::Disconnected; }
    auto NetClient::last_error() const -> std::string { return backend_ != nullptr ? backend_->last_error() : error_; }
    auto NetClient::rtt_ms() const -> std::uint32_t { return backend_ != nullptr ? backend_->rtt_ms() : 0; }

    void NetClient::send_bytes(PacketType type, std::span<std::byte const> payload, Channel channel)
    {
        if (state() != State::Connected) { return; }
        inbox_.count_sent(1, payload.size() + 1);
        backend_->send(type, payload, channel);
    }

    // --- NetServer --------------------------------------------------------------------------------

    NetServer::NetServer() : shared_{std::make_unique<detail::ServerShared>()} {}
    NetServer::~NetServer() { stop(); }

    auto NetServer::inbox() -> PacketInbox & { return shared_->inbox; }

    auto NetServer::start(ServerConfig const & config) -> bool
    {
        if (backend_ != nullptr) { return false; }
        error_.clear();
        shared_->inbox.clear();
        shared_->events.clear();

        std::unique_ptr<detail::ServerBackend> backend;
        switch (config.kind)
        {
        case TransportKind::Enet: backend = detail::make_enet_server(*shared_); break;
        case TransportKind::Steam: backend = detail::make_steam_server(*shared_, error_); break;
        case TransportKind::Loopback: backend = detail::make_loopback_server(*shared_); break;
        }
        if (backend == nullptr)
        {
            if (error_.empty()) { error_ = "unknown transport"; }
            return false;
        }
        if (!backend->start(config))
        {
            error_ = backend->last_error();
            return false;
        }
        if (simulation_.enabled()) { backend = detail::make_simulated(std::move(backend), simulation_); }
        backend_ = std::move(backend);
        buckets_.assign(config.max_clients, Bucket{});
        shared_->inbox.set_admission([this](PeerId peer, std::size_t bytes) { return admit(peer, bytes); });
        return true;
    }

    void NetServer::stop()
    {
        if (backend_ == nullptr) { return; }
        backend_->stop();
        backend_.reset();
        shared_->inbox.set_admission({});
    }

    void NetServer::update()
    {
        if (backend_ == nullptr) { return; }
        auto const events_before = shared_->events.size();
        backend_->update();
        // A new connection starts with full buckets (the slot may have belonged to a flooder before).
        for (auto i = events_before; i < shared_->events.size(); ++i)
        {
            if (shared_->events[i].connected && shared_->events[i].peer < buckets_.size()) { buckets_[shared_->events[i].peer] = Bucket{}; }
        }
    }

    auto NetServer::admit(PeerId peer, std::size_t bytes) -> bool
    {
        if (peer >= buckets_.size()) { return false; }
        auto & bucket = buckets_[peer];
        auto const now = std::chrono::steady_clock::now();
        if (bucket.fresh)
        {
            bucket.packets = limit_.packet_burst;
            bucket.bytes = limit_.byte_burst;
            bucket.fresh = false;
        }
        else
        {
            auto const elapsed = std::chrono::duration<double>(now - bucket.last).count();
            bucket.packets = std::min(limit_.packet_burst, bucket.packets + elapsed * limit_.packets_per_second);
            bucket.bytes = std::min(limit_.byte_burst, bucket.bytes + elapsed * limit_.bytes_per_second);
        }
        bucket.last = now;
        if (bucket.packets < 1.0 || bucket.bytes < static_cast<double>(bytes))
        {
            ++bucket.violations;
            return false;
        }
        bucket.packets -= 1.0;
        bucket.bytes -= static_cast<double>(bytes);
        return true;
    }

    auto NetServer::take_rate_violations(PeerId peer) -> std::uint32_t
    {
        if (peer >= buckets_.size()) { return 0; }
        return std::exchange(buckets_[peer].violations, 0u);
    }

    auto NetServer::stats() const -> TrafficStats const & { return shared_->inbox.stats(); }

    auto NetServer::is_running() const -> bool { return backend_ != nullptr; }
    auto NetServer::client_count() const -> std::size_t { return backend_ != nullptr ? backend_->client_count() : 0; }
    auto NetServer::is_connected(PeerId peer) const -> bool { return backend_ != nullptr && backend_->is_connected(peer); }
    auto NetServer::rtt_ms(PeerId peer) const -> std::uint32_t { return backend_ != nullptr ? backend_->rtt_ms(peer) : 0; }
    auto NetServer::identity(PeerId peer) const -> std::uint64_t { return backend_ != nullptr ? backend_->identity(peer) : 0; }

    void NetServer::disconnect_peer(PeerId peer)
    {
        if (backend_ != nullptr) { backend_->disconnect_peer(peer); }
    }

    void NetServer::send_bytes_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel channel)
    {
        if (backend_ == nullptr || !backend_->is_connected(peer)) { return; }
        shared_->inbox.count_sent(1, payload.size() + 1);
        backend_->send_to(peer, type, payload, channel);
    }

    void NetServer::broadcast_bytes(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except)
    {
        if (backend_ == nullptr) { return; }
        auto recipients = backend_->client_count();
        if (except && backend_->is_connected(*except)) { --recipients; }
        shared_->inbox.count_sent(recipients, recipients * (payload.size() + 1));
        backend_->broadcast(type, payload, channel, except);
    }

    auto NetServer::poll_connection_event() -> std::optional<ConnectionEvent>
    {
        if (shared_->events.empty()) { return std::nullopt; }
        auto const event = shared_->events.front();
        shared_->events.pop_front();
        return event;
    }
} // namespace t3d::net
