#include "net/network_manager.hpp"

#include "core/log.hpp"

namespace t3d::net
{
    namespace
    {
        auto kind_name(TransportKind kind) -> char const *
        {
            switch (kind)
            {
            case TransportKind::Steam: return "steam";
            case TransportKind::Loopback: return "loopback";
            default: return "enet";
            }
        }
    } // namespace

    NetworkManager::NetworkManager() = default;
    NetworkManager::~NetworkManager() { disconnect(); }

    auto NetworkManager::session_config(std::uint32_t max_players) const -> SessionConfig
    {
        SessionConfig config;
        config.content_hash = options_.content_hash;
        config.max_players = max_players;
        config.allow_duplicate_identity = options_.allow_duplicate_identity;
        return config;
    }

    auto NetworkManager::start_host(ServerConfig const & config) -> bool
    {
        if (state() != State::Idle) { return false; }
        last_error_.clear();

        server_ = std::make_unique<NetServer>();
        server_->set_simulation(options_.simulation);
        server_->set_rate_limit(options_.rate_limit);
        if (!server_->start(config))
        {
            last_error_ = server_->last_error();
            server_.reset();
            return false;
        }
        server_session_ = std::make_unique<ServerSession>(*server_, session_config(config.max_clients));

        // The host plays through a normal client connection so there is a single code path.
        client_ = std::make_unique<NetClient>();
        client_->set_simulation(options_.simulation);
        Endpoint local;
        switch (config.kind)
        {
        case TransportKind::Steam: local = Endpoint::steam_direct("127.0.0.1", config.port); break;
        case TransportKind::Loopback: local = Endpoint::loopback(config.port); break;
        case TransportKind::Enet: local = Endpoint::enet("127.0.0.1", config.port); break;
        }
        if (!client_->connect(local))
        {
            last_error_ = client_->last_error();
            disconnect();
            return false;
        }
        client_session_ = std::make_unique<ClientSession>(*client_, session_config(0), options_.username);
        log_info("net: hosting ({}) on port {}", kind_name(config.kind), config.port);
        return true;
    }

    auto NetworkManager::connect(Endpoint const & endpoint) -> bool
    {
        if (state() != State::Idle) { return false; }
        last_error_.clear();

        client_ = std::make_unique<NetClient>();
        client_->set_simulation(options_.simulation);
        if (!client_->connect(endpoint))
        {
            last_error_ = client_->last_error();
            client_.reset();
            return false;
        }
        client_session_ = std::make_unique<ClientSession>(*client_, session_config(0), options_.username);
        log_info("net: connecting ({})", kind_name(endpoint.kind));
        return true;
    }

    void NetworkManager::disconnect()
    {
        // Client first so the server can still acknowledge its graceful disconnect.
        client_session_.reset();
        client_.reset();
        server_session_.reset();
        server_.reset();
    }

    void NetworkManager::update()
    {
        if (server_ != nullptr)
        {
            server_->update();
            server_session_->update();
        }
        if (client_ != nullptr)
        {
            client_->update();
            client_session_->update();
            if (client_session_->state() == ClientSession::State::Disconnected)
            {
                // Connection failed, was rejected/kicked, or was lost. When hosting this tears the whole session down too.
                last_error_ = client_session_->disconnect_reason();
                log_warn("net: {}", last_error_);
                disconnect();
            }
        }
    }

    auto NetworkManager::state() const -> State
    {
        if (server_ != nullptr) { return State::Hosting; }
        if (client_ == nullptr) { return State::Idle; }
        return client_->is_connected() ? State::Connected : State::Connecting;
    }

    auto NetworkManager::rtt_ms() const -> std::uint32_t
    {
        if (client_session_ != nullptr && client_session_->rtt_ms() != 0) { return client_session_->rtt_ms(); }
        return client_ != nullptr ? client_->rtt_ms() : 0;
    }

    auto NetworkManager::stats() const -> TrafficStats
    {
        TrafficStats total;
        for (auto const * s : {client_ != nullptr ? &client_->stats() : nullptr, server_ != nullptr ? &server_->stats() : nullptr})
        {
            if (s == nullptr) { continue; }
            total.packets_in += s->packets_in;
            total.bytes_in += s->bytes_in;
            total.packets_out += s->packets_out;
            total.bytes_out += s->bytes_out;
        }
        return total;
    }
} // namespace t3d::net
