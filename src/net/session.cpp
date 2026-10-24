#include "net/session.hpp"

#include "core/log.hpp"

#include <algorithm>
#include <cctype>
#include <cstring>

namespace t3d::net
{
    namespace
    {
        constexpr std::size_t MAX_USERNAME_LENGTH = 16;
        constexpr std::size_t MAX_PEERS = 65536;
        constexpr auto PING_INTERVAL = std::chrono::seconds{1};
        constexpr auto CLIENT_HANDSHAKE_TIMEOUT = std::chrono::seconds{10};

        template <std::size_t N>
        void copy_text(char (&dst)[N], std::string_view text)
        {
            std::memset(dst, 0, N);
            std::memcpy(dst, text.data(), std::min(text.size(), N - 1));
        }

        template <std::size_t N>
        auto read_text(char const (&src)[N]) -> std::string
        {
            return {src, strnlen(src, N)};
        }

        auto sanitize_username(std::string_view raw) -> std::string
        {
            std::string name;
            for (char c : raw)
            {
                auto const u = static_cast<unsigned char>(c);
                name += (std::isalnum(u) != 0 || c == ' ' || c == '_' || c == '-') && u < 128 ? c : '_';
            }
            auto const first = name.find_first_not_of(' ');
            if (first == std::string::npos) { return {}; }
            name = name.substr(first, name.find_last_not_of(' ') - first + 1);
            if (name.size() > MAX_USERNAME_LENGTH) { name.resize(MAX_USERNAME_LENGTH); }
            return name;
        }
    } // namespace

    // =============================================================================================
    // ServerSession

    ServerSession::ServerSession(NetServer & server, SessionConfig config) : server_{server}, config_{config} {}

    auto ServerSession::peer_at(PeerId peer) -> Peer *
    {
        return peer < peers_.size() && peers_[peer] ? &*peers_[peer] : nullptr;
    }
    auto ServerSession::peer_at(PeerId peer) const -> Peer const *
    {
        return peer < peers_.size() && peers_[peer] ? &*peers_[peer] : nullptr;
    }

    void ServerSession::update()
    {
        while (auto event = server_.poll_connection_event())
        {
            if (event->connected) { on_connected(event->peer); }
            else { on_disconnected(event->peer); }
        }

        while (auto message = server_.receive<HandshakePacket>())
        {
            handle_handshake(message->second, message->first);
        }

        while (auto message = server_.receive<PingPacket>())
        {
            if (peer_at(message->second) != nullptr)
            {
                server_.send_to(message->second, PongPacket{.timestamp_ms = message->first.timestamp_ms}, Channel::Unreliable);
            }
        }

        auto const now = std::chrono::steady_clock::now();
        for (PeerId id = 0; id < peers_.size(); ++id)
        {
            Peer * peer = peer_at(id);
            if (peer == nullptr || peer->state == State::Closing) { continue; }

            if ((peer->state == State::Handshaking || peer->state == State::Validated) && now > peer->deadline)
            {
                if (peer->state == State::Validated) { reject(id, RejectReason::Timeout, "Server did not accept you in time"); }
                else { send_reject(id, RejectReason::Timeout, "Handshake timed out"); }
                continue;
            }

            // Flood protection: the transport drops excess packets, we kick persistent offenders.
            peer->violations += server_.take_rate_violations(id);
            if (now - peer->violation_window_start > std::chrono::seconds{1})
            {
                peer->violations = 0;
                peer->violation_window_start = now;
            }
            else if (peer->violations > config_.max_rate_violations_per_second)
            {
                log_warn("session: kicking peer {} for flooding", id);
                if (peer->state == State::Active) { kick(id, "Too many packets"); }
                else { send_reject(id, RejectReason::Malformed, "Too many packets"); }
            }
        }
    }

    void ServerSession::on_connected(PeerId id)
    {
        if (id >= MAX_PEERS) { server_.disconnect_peer(id); return; }
        if (id >= peers_.size()) { peers_.resize(id + 1); }
        auto const now = std::chrono::steady_clock::now();
        peers_[id] = Peer{.deadline = now + config_.handshake_timeout, .violation_window_start = now};
    }

    void ServerSession::on_disconnected(PeerId id)
    {
        Peer * peer = peer_at(id);
        if (peer == nullptr) { return; }
        if (peer->joined)
        {
            events_.push_back({.type = SessionEvent::Type::Left, .peer = id, .player_id = peer->player_id, .identity = peer->identity,
                               .username = peer->username, .was_active = peer->was_active});
        }
        peers_[id].reset();
    }

    void ServerSession::handle_handshake(PeerId id, HandshakePacket const & packet)
    {
        Peer * peer = peer_at(id);
        if (peer == nullptr || peer->state != State::Handshaking) { return; } // duplicate / late handshake: ignore

        auto const identity = server_.identity(id);
        if (packet.protocol_version != config_.protocol_version)
        {
            send_reject(id, RejectReason::VersionMismatch,
                        "Version mismatch (server " + std::to_string(config_.protocol_version) + ", you " + std::to_string(packet.protocol_version) + ")");
            return;
        }
        if (packet.content_hash != config_.content_hash)
        {
            send_reject(id, RejectReason::ContentMismatch, "Game content does not match the server (different mods or version)");
            return;
        }
        if (identity != 0 && is_banned(identity))
        {
            send_reject(id, RejectReason::Banned, "You are banned from this server");
            return;
        }
        if (identity != 0 && !config_.allow_duplicate_identity)
        {
            for (PeerId other = 0; other < peers_.size(); ++other)
            {
                if (other != id && peers_[other] && peers_[other]->identity == identity &&
                    (peers_[other]->state == State::Validated || peers_[other]->state == State::Active))
                {
                    send_reject(id, RejectReason::DuplicateLogin, "You are already connected to this server");
                    return;
                }
            }
        }
        auto const joined = std::ranges::count_if(peers_, [](auto const & p) { return p && (p->state == State::Validated || p->state == State::Active); });
        if (config_.max_players != 0 && static_cast<std::uint32_t>(joined) >= config_.max_players)
        {
            send_reject(id, RejectReason::ServerFull, "Server is full");
            return;
        }
        auto name = sanitize_username(read_text(packet.username));
        if (name.empty())
        {
            send_reject(id, RejectReason::BadUsername, "Invalid username");
            return;
        }
        auto const player = free_player_id();
        if (!player)
        {
            send_reject(id, RejectReason::ServerFull, "Server is full");
            return;
        }

        peer->username = make_unique_username(std::move(name));
        peer->identity = identity;
        peer->player_id = *player;
        peer->state = State::Validated;
        peer->joined = true;
        peer->deadline = std::chrono::steady_clock::now() + config_.accept_timeout;
        events_.push_back({.type = SessionEvent::Type::JoinRequested, .peer = id, .player_id = peer->player_id, .identity = identity, .username = peer->username});
    }

    auto ServerSession::make_unique_username(std::string name) const -> std::string
    {
        auto taken = [&](std::string const & candidate)
        {
            return std::ranges::any_of(peers_, [&](auto const & p) { return p && p->state != State::Handshaking && p->username == candidate; });
        };
        if (!taken(name)) { return name; }
        for (int suffix = 2;; ++suffix)
        {
            auto const tag = "#" + std::to_string(suffix);
            auto candidate = name.substr(0, MAX_USERNAME_LENGTH - tag.size()) + tag;
            if (!taken(candidate)) { return candidate; }
        }
    }

    auto ServerSession::free_player_id() const -> std::optional<PlayerId>
    {
        for (std::size_t candidate = 0; candidate < MAX_PEERS; ++candidate)
        {
            bool used = false;
            for (auto const & p : peers_)
            {
                if (p && (p->state == State::Validated || p->state == State::Active) && p->player_id == candidate) { used = true; break; }
            }
            if (!used) { return static_cast<PlayerId>(candidate); }
        }
        return std::nullopt;
    }

    void ServerSession::accept(PeerId id, HandshakeAcceptPacket info)
    {
        Peer * peer = peer_at(id);
        if (peer == nullptr || peer->state != State::Validated) { return; }
        info.player_id = peer->player_id;
        info.tick_rate = config_.tick_rate;
        copy_text(info.username, peer->username);
        peer->state = State::Active;
        peer->was_active = true;
        server_.send_to(id, info);
    }

    void ServerSession::send_reject(PeerId id, RejectReason code, std::string_view text)
    {
        Peer * peer = peer_at(id);
        if (peer == nullptr || peer->state == State::Closing) { return; }
        HandshakeRejectPacket packet{.code = code};
        copy_text(packet.reason, text);
        server_.send_to(id, packet);
        server_.disconnect_peer(id); // transports deliver already queued reliable packets before closing
        if (peer_at(id) != nullptr)  // some transports report the disconnect immediately, which erases the peer
        {
            peer_at(id)->state = State::Closing;
        }
    }

    void ServerSession::reject(PeerId id, RejectReason code, std::string_view text) { send_reject(id, code, text); }

    void ServerSession::kick(PeerId id, std::string_view reason)
    {
        Peer * peer = peer_at(id);
        if (peer == nullptr || peer->state == State::Closing) { return; }
        KickPacket packet{};
        copy_text(packet.reason, reason);
        server_.send_to(id, packet);
        server_.disconnect_peer(id);
        if (peer_at(id) != nullptr) { peer_at(id)->state = State::Closing; }
    }

    auto ServerSession::poll_event() -> std::optional<SessionEvent>
    {
        if (events_.empty()) { return std::nullopt; }
        auto event = std::move(events_.front());
        events_.pop_front();
        return event;
    }

    auto ServerSession::is_active(PeerId peer) const -> bool
    {
        auto const * p = peer_at(peer);
        return p != nullptr && p->state == State::Active;
    }

    auto ServerSession::player_id(PeerId peer) const -> std::optional<PlayerId>
    {
        auto const * p = peer_at(peer);
        if (p == nullptr || (p->state != State::Validated && p->state != State::Active)) { return std::nullopt; }
        return p->player_id;
    }

    auto ServerSession::peer_of(PlayerId player) const -> std::optional<PeerId>
    {
        for (PeerId id = 0; id < peers_.size(); ++id)
        {
            if (peers_[id] && peers_[id]->state == State::Active && peers_[id]->player_id == player) { return id; }
        }
        return std::nullopt;
    }

    auto ServerSession::username(PeerId peer) const -> std::string
    {
        auto const * p = peer_at(peer);
        return p != nullptr ? p->username : std::string{};
    }

    auto ServerSession::active_count() const -> std::size_t
    {
        return static_cast<std::size_t>(std::ranges::count_if(peers_, [](auto const & p) { return p && p->state == State::Active; }));
    }

    // =============================================================================================
    // ClientSession

    ClientSession::ClientSession(NetClient & client, SessionConfig config, std::string username)
        : client_{client}, config_{config}, username_{std::move(username)}, start_{std::chrono::steady_clock::now()}
    {
        handshake_deadline_ = start_ + CLIENT_HANDSHAKE_TIMEOUT;
    }

    auto ClientSession::now_ms() const -> std::uint32_t
    {
        return static_cast<std::uint32_t>(std::chrono::duration_cast<std::chrono::milliseconds>(std::chrono::steady_clock::now() - start_).count());
    }

    void ClientSession::update()
    {
        if (state_ == State::Disconnected) { return; }

        // Messages that explain a disconnect can arrive in the same update as the disconnect itself: read them first.
        if (auto reject = client_.receive<HandshakeRejectPacket>())
        {
            reject_code_ = reject->code;
            reason_ = read_text(reject->reason);
        }
        if (auto kick = client_.receive<KickPacket>()) { reason_ = read_text(kick->reason); }

        if (client_.state() == NetClient::State::Disconnected)
        {
            if (reason_.empty()) { reason_ = client_.last_error().empty() ? "Disconnected" : client_.last_error(); }
            state_ = State::Disconnected;
            return;
        }

        auto const now = std::chrono::steady_clock::now();
        switch (state_)
        {
        case State::Connecting:
            if (client_.is_connected())
            {
                HandshakePacket handshake{.protocol_version = config_.protocol_version, .content_hash = config_.content_hash};
                copy_text(handshake.username, username_);
                client_.send(handshake);
                state_ = State::Handshaking;
                handshake_deadline_ = now + CLIENT_HANDSHAKE_TIMEOUT;
            }
            break;
        case State::Handshaking:
            if (auto accept = client_.receive<HandshakeAcceptPacket>())
            {
                accept_ = *accept;
                state_ = State::Active;
                next_ping_ = now;
            }
            else if (now > handshake_deadline_)
            {
                reason_ = "Server did not answer the handshake";
                client_.disconnect();
                state_ = State::Disconnected;
            }
            break;
        case State::Active:
            while (auto pong = client_.receive<PongPacket>())
            {
                double const sample = static_cast<double>(now_ms() - pong->timestamp_ms);
                rtt_ms_ = rtt_ms_ == 0 ? sample : rtt_ms_ * 0.8 + sample * 0.2;
            }
            if (now >= next_ping_)
            {
                client_.send(PingPacket{.timestamp_ms = now_ms()}, Channel::Unreliable);
                next_ping_ = now + PING_INTERVAL;
            }
            break;
        case State::Disconnected: break;
        }
    }
} // namespace t3d::net
