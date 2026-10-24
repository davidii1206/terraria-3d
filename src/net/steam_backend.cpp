// Steam transport backend (ISteamNetworkingSockets). See net/transport.hpp and net/steam_platform.hpp.
//
// SERVER listens on two sockets:
//   * P2P listen socket (virtual port 0): remote players connect to the host's SteamID through Valve's relay
//     network. No port forwarding. The peer's identity (SteamID64) is authenticated by Steam.
//   * Loopback-only IP listen socket (127.0.0.1:port): the host's own client connects here, so the hosting
//     player takes exactly the same code path as everyone else (and we never need to "connect to ourselves"
//     over P2P). Identity of this connection is the local Steam user.
// Lanes: Reliable + Unreliable share lane 0, Bulk (chunk streaming) has lane 1, so a big chunk transfer
// cannot delay gameplay messages.

#include "core/log.hpp"
#include "net/transport_backend.hpp"

#if defined(T3D_STEAM)
#include "net/steam_platform.hpp"

#include <steam/steam_api.h>

#include <algorithm>
#include <thread>
#include <unordered_map>

namespace t3d::net::detail
{
    namespace
    {
        constexpr int MAX_RECEIVE_BATCH = 64;
        constexpr int LANE_GAMEPLAY = 0;
        constexpr int LANE_BULK = 1;
        constexpr int VIRTUAL_PORT = 0;
        constexpr auto SHUTDOWN_LINGER = std::chrono::milliseconds{200};

        // --- connection status callback routing --------------------------------------------------
        // Steam calls one C function for every connection we configured it on. Several backends can exist at
        // once (host = server + client), so each registers itself and claims the connections it owns.
        class StatusListener
        {
          public:
            virtual ~StatusListener() = default;
            virtual auto on_status_changed(SteamNetConnectionStatusChangedCallback_t const & info) -> bool = 0; // true = handled
        };

        std::vector<StatusListener *> & listeners()
        {
            static std::vector<StatusListener *> list;
            return list;
        }

        void status_callback(SteamNetConnectionStatusChangedCallback_t * info)
        {
            for (auto * listener : listeners())
            {
                if (listener->on_status_changed(*info)) { return; }
            }
        }

        struct ListenerRegistration
        {
            explicit ListenerRegistration(StatusListener * l) : listener{l} { listeners().push_back(l); }
            ~ListenerRegistration() { std::erase(listeners(), listener); }
            ListenerRegistration(ListenerRegistration const &) = delete;
            auto operator=(ListenerRegistration const &) -> ListenerRegistration & = delete;
            StatusListener * listener;
        };

        auto status_option() -> SteamNetworkingConfigValue_t
        {
            SteamNetworkingConfigValue_t option;
            option.SetPtr(k_ESteamNetworkingConfig_Callback_ConnectionStatusChanged, reinterpret_cast<void *>(&status_callback));
            return option;
        }

        void configure_lanes(HSteamNetConnection connection)
        {
            // Equal priority, gameplay gets 4x the share of bulk when the link is saturated.
            int const priorities[2] = {0, 0};
            std::uint16_t const weights[2] = {4, 1};
            SteamNetworkingSockets()->ConfigureConnectionLanes(connection, 2, priorities, weights);
        }

        auto make_message(HSteamNetConnection connection, PacketType type, std::span<std::byte const> payload, Channel channel)
            -> SteamNetworkingMessage_t *
        {
            SteamNetworkingMessage_t * message = SteamNetworkingUtils()->AllocateMessage(static_cast<int>(payload.size() + 1));
            if (message == nullptr) { return nullptr; }
            auto * bytes = static_cast<std::uint8_t *>(message->m_pData);
            bytes[0] = static_cast<std::uint8_t>(type);
            if (!payload.empty()) { std::memcpy(bytes + 1, payload.data(), payload.size()); }
            message->m_conn = connection;
            switch (channel)
            {
            case Channel::Unreliable: message->m_nFlags = k_nSteamNetworkingSend_UnreliableNoNagle; break;
            case Channel::Bulk: message->m_nFlags = k_nSteamNetworkingSend_Reliable; break;
            default: message->m_nFlags = k_nSteamNetworkingSend_ReliableNoNagle; break;
            }
            message->m_idxLane = static_cast<std::uint16_t>(channel == Channel::Bulk ? LANE_BULK : LANE_GAMEPLAY);
            return message;
        }

        void send_message(SteamNetworkingMessage_t * message)
        {
            if (message != nullptr) { SteamNetworkingSockets()->SendMessages(1, &message, nullptr, true); }
        }

        auto rtt_of(HSteamNetConnection connection) -> std::uint32_t
        {
            SteamNetConnectionRealTimeStatus_t status{};
            if (SteamNetworkingSockets()->GetConnectionRealTimeStatus(connection, &status, 0, nullptr) != k_EResultOK) { return 0; }
            return status.m_nPing > 0 ? static_cast<std::uint32_t>(status.m_nPing) : 0;
        }

        // =========================================================================================
        class SteamClientBackend final : public ClientBackend, private StatusListener
        {
          public:
            explicit SteamClientBackend(PacketInbox & inbox) : inbox_{inbox}, registration_{this} {}
            ~SteamClientBackend() override { SteamClientBackend::disconnect(); }

            auto connect(Endpoint const & endpoint, std::chrono::milliseconds timeout) -> bool override
            {
                if (state_ != ClientState::Disconnected) { return false; }
                error_.clear();

                auto const option = status_option();
                auto * sockets = SteamNetworkingSockets();
                if (endpoint.steam_id != 0)
                {
                    SteamNetworkingIdentity identity;
                    identity.SetSteamID64(endpoint.steam_id);
                    connection_ = sockets->ConnectP2P(identity, VIRTUAL_PORT, 1, &option);
                }
                else
                {
                    SteamNetworkingIPAddr address;
                    address.Clear();
                    auto const text = endpoint.host + ":" + std::to_string(endpoint.port);
                    if (!address.ParseString(text.c_str()))
                    {
                        error_ = "Steam direct connections need an IP address, got '" + endpoint.host + "'";
                        return false;
                    }
                    connection_ = sockets->ConnectByIPAddress(address, 1, &option);
                }
                if (connection_ == k_HSteamNetConnection_Invalid)
                {
                    error_ = "could not start Steam connection";
                    return false;
                }
                configure_lanes(connection_);
                state_ = ClientState::Connecting;
                deadline_ = std::chrono::steady_clock::now() + timeout;
                return true;
            }

            void disconnect() override
            {
                close_connection();
            }

            void update() override
            {
                if (connection_ == k_HSteamNetConnection_Invalid) { return; }
                SteamNetworkingSockets()->RunCallbacks(); // may close the connection (see on_status_changed)
                if (connection_ == k_HSteamNetConnection_Invalid) { return; }

                if (state_ == ClientState::Connecting && std::chrono::steady_clock::now() > deadline_)
                {
                    error_ = "connection timed out";
                    close_connection();
                    return;
                }
                SteamNetworkingMessage_t * messages[MAX_RECEIVE_BATCH];
                int count;
                while (connection_ != k_HSteamNetConnection_Invalid &&
                       (count = SteamNetworkingSockets()->ReceiveMessagesOnConnection(connection_, messages, MAX_RECEIVE_BATCH)) > 0)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        deliver_frame({static_cast<std::uint8_t const *>(messages[i]->m_pData), static_cast<std::size_t>(messages[i]->m_cbSize)}, 0, inbox_);
                        messages[i]->Release();
                    }
                }
            }

            [[nodiscard]] auto state() const -> ClientState override { return state_; }
            [[nodiscard]] auto last_error() const -> std::string override { return error_; }
            [[nodiscard]] auto rtt_ms() const -> std::uint32_t override
            {
                return connection_ != k_HSteamNetConnection_Invalid ? rtt_of(connection_) : 0;
            }

            void send(PacketType type, std::span<std::byte const> payload, Channel channel) override
            {
                if (state_ == ClientState::Connected) { send_message(make_message(connection_, type, payload, channel)); }
            }

          private:
            auto on_status_changed(SteamNetConnectionStatusChangedCallback_t const & info) -> bool override
            {
                if (info.m_hConn != connection_ || connection_ == k_HSteamNetConnection_Invalid) { return false; }
                switch (info.m_info.m_eState)
                {
                case k_ESteamNetworkingConnectionState_Connected: state_ = ClientState::Connected; break;
                case k_ESteamNetworkingConnectionState_ClosedByPeer:
                case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                    error_ = info.m_info.m_szEndDebug[0] != '\0' ? info.m_info.m_szEndDebug
                             : state_ == ClientState::Connecting ? "could not reach host"
                                                                 : "connection lost";
                    close_connection(); // inbox keeps anything already received (e.g. a Kick reason)
                    break;
                default: break;
                }
                return true;
            }

            void close_connection()
            {
                if (connection_ != k_HSteamNetConnection_Invalid)
                {
                    SteamNetworkingSockets()->CloseConnection(connection_, 0, "client disconnect", true);
                    connection_ = k_HSteamNetConnection_Invalid;
                }
                state_ = ClientState::Disconnected;
            }

            PacketInbox & inbox_;
            ListenerRegistration registration_;
            HSteamNetConnection connection_ = k_HSteamNetConnection_Invalid;
            ClientState state_ = ClientState::Disconnected;
            std::chrono::steady_clock::time_point deadline_{};
            std::string error_;
        };

        // =========================================================================================
        class SteamServerBackend final : public ServerBackend, private StatusListener
        {
          public:
            explicit SteamServerBackend(ServerShared & shared) : shared_{shared}, registration_{this} {}
            ~SteamServerBackend() override { SteamServerBackend::stop(); }

            auto start(ServerConfig const & config) -> bool override
            {
                auto * sockets = SteamNetworkingSockets();
                auto const option = status_option();

                listen_p2p_ = sockets->CreateListenSocketP2P(VIRTUAL_PORT, 1, &option);
                if (listen_p2p_ == k_HSteamListenSocket_Invalid)
                {
                    error_ = "could not create Steam P2P listen socket";
                    return false;
                }
                SteamNetworkingIPAddr loopback;
                loopback.Clear();
                loopback.SetIPv4(0x7F000001, config.port);
                listen_ip_ = sockets->CreateListenSocketIP(loopback, 1, &option);
                if (listen_ip_ == k_HSteamListenSocket_Invalid)
                {
                    error_ = "could not listen on 127.0.0.1:" + std::to_string(config.port) + " (already in use?)";
                    sockets->CloseListenSocket(listen_p2p_);
                    listen_p2p_ = k_HSteamListenSocket_Invalid;
                    return false;
                }
                poll_group_ = sockets->CreatePollGroup();
                slots_.assign(config.max_clients, Slot{});
                return true;
            }

            void stop() override
            {
                if (listen_p2p_ == k_HSteamListenSocket_Invalid) { return; }
                auto * sockets = SteamNetworkingSockets();
                bool had_clients = false;
                for (Slot & slot : slots_)
                {
                    if (slot.connection == k_HSteamNetConnection_Invalid) { continue; }
                    had_clients = true;
                    sockets->FlushMessagesOnConnection(slot.connection);
                    sockets->CloseConnection(slot.connection, 0, "server shutting down", true);
                    slot = Slot{};
                }
                if (had_clients) { std::this_thread::sleep_for(SHUTDOWN_LINGER); } // let the close notices leave
                by_connection_.clear();
                sockets->CloseListenSocket(listen_p2p_);
                sockets->CloseListenSocket(listen_ip_);
                sockets->DestroyPollGroup(poll_group_);
                listen_p2p_ = listen_ip_ = k_HSteamListenSocket_Invalid;
                poll_group_ = k_HSteamNetPollGroup_Invalid;
                slots_.clear();
            }

            void update() override
            {
                if (poll_group_ == k_HSteamNetPollGroup_Invalid) { return; }
                SteamNetworkingSockets()->RunCallbacks();

                SteamNetworkingMessage_t * messages[MAX_RECEIVE_BATCH];
                int count;
                while ((count = SteamNetworkingSockets()->ReceiveMessagesOnPollGroup(poll_group_, messages, MAX_RECEIVE_BATCH)) > 0)
                {
                    for (int i = 0; i < count; ++i)
                    {
                        if (auto const it = by_connection_.find(messages[i]->m_conn); it != by_connection_.end())
                        {
                            deliver_frame({static_cast<std::uint8_t const *>(messages[i]->m_pData), static_cast<std::size_t>(messages[i]->m_cbSize)},
                                          it->second, shared_.inbox);
                        }
                        messages[i]->Release();
                    }
                }
            }

            [[nodiscard]] auto last_error() const -> std::string override { return error_; }
            [[nodiscard]] auto client_count() const -> std::size_t override
            {
                return static_cast<std::size_t>(std::ranges::count_if(slots_, [](Slot const & s) { return s.announced; }));
            }
            [[nodiscard]] auto is_connected(PeerId peer) const -> bool override { return peer < slots_.size() && slots_[peer].announced; }
            [[nodiscard]] auto rtt_ms(PeerId peer) const -> std::uint32_t override
            {
                return is_connected(peer) ? rtt_of(slots_[peer].connection) : 0;
            }
            [[nodiscard]] auto identity(PeerId peer) const -> std::uint64_t override
            {
                return is_connected(peer) ? slots_[peer].identity : 0;
            }

            void disconnect_peer(PeerId peer) override
            {
                if (!is_connected(peer)) { return; }
                // After CloseConnection Steam sends no more status callbacks for it, so finish the bookkeeping here.
                SteamNetworkingSockets()->FlushMessagesOnConnection(slots_[peer].connection);
                SteamNetworkingSockets()->CloseConnection(slots_[peer].connection, 0, "kicked", true);
                release_slot(peer);
            }

            void send_to(PeerId peer, PacketType type, std::span<std::byte const> payload, Channel channel) override
            {
                if (is_connected(peer)) { send_message(make_message(slots_[peer].connection, type, payload, channel)); }
            }

            void broadcast(PacketType type, std::span<std::byte const> payload, Channel channel, std::optional<PeerId> except) override
            {
                for (PeerId id = 0; id < slots_.size(); ++id)
                {
                    if (slots_[id].announced && id != except) { send_message(make_message(slots_[id].connection, type, payload, channel)); }
                }
            }

          private:
            struct Slot
            {
                HSteamNetConnection connection = k_HSteamNetConnection_Invalid;
                std::uint64_t identity = 0;
                bool announced = false; // "connected" event emitted
            };

            auto on_status_changed(SteamNetConnectionStatusChangedCallback_t const & info) -> bool override
            {
                auto const listen = info.m_info.m_hListenSocket;
                bool const ours_incoming = listen != k_HSteamListenSocket_Invalid && (listen == listen_p2p_ || listen == listen_ip_);
                auto const known = by_connection_.find(info.m_hConn);
                if (!ours_incoming && known == by_connection_.end()) { return false; }

                auto * sockets = SteamNetworkingSockets();
                switch (info.m_info.m_eState)
                {
                case k_ESteamNetworkingConnectionState_Connecting:
                {
                    if (known != by_connection_.end()) { break; }
                    auto const free_slot = std::ranges::find_if(slots_, [](Slot const & s) { return s.connection == k_HSteamNetConnection_Invalid; });
                    if (free_slot == slots_.end())
                    {
                        sockets->CloseConnection(info.m_hConn, 0, "server full", false);
                        break;
                    }
                    if (sockets->AcceptConnection(info.m_hConn) != k_EResultOK)
                    {
                        sockets->CloseConnection(info.m_hConn, 0, "accept failed", false);
                        break;
                    }
                    sockets->SetConnectionPollGroup(info.m_hConn, poll_group_);
                    configure_lanes(info.m_hConn);
                    auto const id = static_cast<PeerId>(free_slot - slots_.begin());
                    free_slot->connection = info.m_hConn;
                    // Remote players: Steam-verified SteamID. The loopback listener only accepts this machine,
                    // so that connection is the host player.
                    free_slot->identity = listen == listen_ip_ ? SteamUser()->GetSteamID().ConvertToUint64()
                                                               : info.m_info.m_identityRemote.GetSteamID64();
                    by_connection_[info.m_hConn] = id;
                    break;
                }
                case k_ESteamNetworkingConnectionState_Connected:
                    if (known != by_connection_.end() && !slots_[known->second].announced)
                    {
                        slots_[known->second].announced = true;
                        shared_.events.push_back({.peer = known->second, .connected = true});
                    }
                    break;
                case k_ESteamNetworkingConnectionState_ClosedByPeer:
                case k_ESteamNetworkingConnectionState_ProblemDetectedLocally:
                    sockets->CloseConnection(info.m_hConn, 0, nullptr, false);
                    if (known != by_connection_.end()) { release_slot(known->second); }
                    break;
                default: break;
                }
                return true;
            }

            void release_slot(PeerId id)
            {
                Slot & slot = slots_[id];
                by_connection_.erase(slot.connection);
                if (slot.announced) { shared_.events.push_back({.peer = id, .connected = false}); }
                slot = Slot{};
            }

            ServerShared & shared_;
            ListenerRegistration registration_;
            HSteamListenSocket listen_p2p_ = k_HSteamListenSocket_Invalid;
            HSteamListenSocket listen_ip_ = k_HSteamListenSocket_Invalid;
            HSteamNetPollGroup poll_group_ = k_HSteamNetPollGroup_Invalid;
            std::vector<Slot> slots_;
            std::unordered_map<HSteamNetConnection, PeerId> by_connection_;
            std::string error_;
        };
    } // namespace

    auto make_steam_client(PacketInbox & inbox, std::string & error) -> std::unique_ptr<ClientBackend>
    {
        if (!SteamPlatform::available())
        {
            error = "Steam is not initialised (is the Steam client running?)";
            return nullptr;
        }
        return std::make_unique<SteamClientBackend>(inbox);
    }

    auto make_steam_server(ServerShared & shared, std::string & error) -> std::unique_ptr<ServerBackend>
    {
        if (!SteamPlatform::available())
        {
            error = "Steam is not initialised (is the Steam client running?)";
            return nullptr;
        }
        return std::make_unique<SteamServerBackend>(shared);
    }
} // namespace t3d::net::detail

#else // ---------------------------------------------------------- built without the Steamworks SDK

namespace t3d::net::detail
{
    auto make_steam_client(PacketInbox &, std::string & error) -> std::unique_ptr<ClientBackend>
    {
        error = "this build has no Steamworks support";
        return nullptr;
    }
    auto make_steam_server(ServerShared &, std::string & error) -> std::unique_ptr<ServerBackend>
    {
        error = "this build has no Steamworks support";
        return nullptr;
    }
} // namespace t3d::net::detail
#endif
