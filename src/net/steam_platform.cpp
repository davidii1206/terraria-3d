#include "net/steam_platform.hpp"

#include "core/log.hpp"

#include <atomic>
#include <cstring>
#include <utility>

#if defined(T3D_STEAM)
#include <steam/steam_api.h>
#endif

namespace t3d::net
{
    namespace
    {
        std::atomic<bool> g_available{false};
    }

    auto SteamPlatform::available() -> bool { return g_available.load(); }

#if defined(T3D_STEAM)
    namespace
    {
        constexpr char JOIN_PREFIX[] = "+connect_steam ";

        auto parse_join_string(char const * text) -> std::optional<std::uint64_t>
        {
            auto const * at = std::strstr(text, JOIN_PREFIX);
            if (at == nullptr) { return std::nullopt; }
            auto const value = std::strtoull(at + sizeof(JOIN_PREFIX) - 1, nullptr, 10);
            if (value == 0) { return std::nullopt; }
            return value;
        }

        void on_steam_warning(int severity, char const * message)
        {
            if (severity >= 1 /* 0 = info, 1 = warning */) { log_warn("steam: {}", message); }
        }
    } // namespace

    struct SteamPlatform::Impl
    {
        std::optional<std::uint64_t> join_request;

        // Fired when the user clicks "Join Game" on a friend while the game is running.
        STEAM_CALLBACK(Impl, on_join_requested, GameRichPresenceJoinRequested_t);
    };

    void SteamPlatform::Impl::on_join_requested(GameRichPresenceJoinRequested_t * data)
    {
        if (auto const id = parse_join_string(data->m_rgchConnect)) { join_request = id; }
    }

    SteamPlatform::SteamPlatform() = default;
    SteamPlatform::~SteamPlatform() { shutdown(); }

    auto SteamPlatform::compiled_in() -> bool { return true; }

    auto SteamPlatform::init() -> bool
    {
        if (impl_ != nullptr) { return true; }
        SteamErrMsg message{};
        if (SteamAPI_InitEx(&message) != k_ESteamAPIInitResult_OK)
        {
            error_ = message;
            return false;
        }
        SteamClient()->SetWarningMessageHook(&on_steam_warning);
        // Start probing the relay network now so the first connection is fast.
        SteamNetworkingUtils()->InitRelayNetworkAccess();
        impl_ = std::make_unique<Impl>();
        g_available = true;
        log_info("steam: ready as '{}' ({})", local_name(), local_id());
        return true;
    }

    void SteamPlatform::shutdown()
    {
        if (impl_ == nullptr) { return; }
        g_available = false;
        publish_join_info(false);
        impl_.reset(); // unregisters callbacks before the API goes away
        SteamAPI_Shutdown();
    }

    void SteamPlatform::update()
    {
        if (impl_ != nullptr) { SteamAPI_RunCallbacks(); }
    }

    auto SteamPlatform::local_id() const -> std::uint64_t
    {
        return impl_ != nullptr ? SteamUser()->GetSteamID().ConvertToUint64() : 0;
    }

    auto SteamPlatform::local_name() const -> std::string
    {
        return impl_ != nullptr ? SteamFriends()->GetPersonaName() : std::string{};
    }

    auto SteamPlatform::relay_status() const -> std::string
    {
        if (impl_ == nullptr) { return "steam not initialised"; }
        SteamRelayNetworkStatus_t status{};
        SteamNetworkingUtils()->GetRelayNetworkStatus(&status);
        switch (status.m_eAvail)
        {
        case k_ESteamNetworkingAvailability_Current: return "ready";
        case k_ESteamNetworkingAvailability_Attempting:
        case k_ESteamNetworkingAvailability_Waiting:
        case k_ESteamNetworkingAvailability_Retrying: return "connecting to relay";
        case k_ESteamNetworkingAvailability_NeverTried: return "not started";
        default: return status.m_debugMsg;
        }
    }

    void SteamPlatform::publish_join_info(bool hosting)
    {
        if (impl_ == nullptr) { return; }
        if (hosting)
        {
            auto const connect = std::string{JOIN_PREFIX} + std::to_string(local_id());
            SteamFriends()->SetRichPresence("connect", connect.c_str());
            SteamFriends()->SetRichPresence("status", "Hosting a world");
        }
        else
        {
            SteamFriends()->ClearRichPresence();
        }
    }

    void SteamPlatform::parse_command_line(int argc, char const * const * argv)
    {
        if (impl_ == nullptr) { return; }
        for (int i = 1; i + 1 < argc; ++i)
        {
            if (std::strcmp(argv[i], "+connect_steam") == 0)
            {
                if (auto const id = std::strtoull(argv[i + 1], nullptr, 10); id != 0) { impl_->join_request = id; }
            }
        }
    }

    auto SteamPlatform::take_join_request() -> std::optional<std::uint64_t>
    {
        if (impl_ == nullptr) { return std::nullopt; }
        return std::exchange(impl_->join_request, std::nullopt);
    }

#else // ----------------------------------------------- built without the Steamworks SDK

    struct SteamPlatform::Impl {};
    SteamPlatform::SteamPlatform() = default;
    SteamPlatform::~SteamPlatform() = default;
    auto SteamPlatform::compiled_in() -> bool { return false; }
    auto SteamPlatform::init() -> bool
    {
        error_ = "built without Steamworks support (put the SDK in ./sdk and reconfigure)";
        return false;
    }
    void SteamPlatform::shutdown() {}
    void SteamPlatform::update() {}
    auto SteamPlatform::local_id() const -> std::uint64_t { return 0; }
    auto SteamPlatform::local_name() const -> std::string { return {}; }
    auto SteamPlatform::relay_status() const -> std::string { return "steam not available"; }
    void SteamPlatform::publish_join_info(bool) {}
    void SteamPlatform::parse_command_line(int, char const * const *) {}
    auto SteamPlatform::take_join_request() -> std::optional<std::uint64_t> { return std::nullopt; }
#endif
} // namespace t3d::net
