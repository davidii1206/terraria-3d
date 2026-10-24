#pragma once
// Steamworks lifecycle + the few Steam features networking needs (identity, invites, relay status).
//
// USAGE
//   SteamPlatform steam;                 // create once, early in startup, keep alive for the whole run
//   steam.init();                        // false => no Steam client / wrong build; the game should fall back to ENet
//   ... every frame: steam.update();     // pumps Steam callbacks (overlay events, join requests)
//   net.start_host({.kind = Steam});     // transports check SteamPlatform::available()
//
// DEVELOPMENT SETUP
//   * The Steam client must be running and logged in.
//   * App ID 480 (Spacewar) is used until the game has its own; a steam_appid.txt next to the executable
//     (written by CMake) lets the exe run outside Steam. Remove that file for shipping builds.
//   * Two instances on the SAME Steam account cannot play against each other over the relay: test relay
//     connections with two accounts on two machines. Local testing uses the loopback listener instead
//     (see steam_backend.cpp), which is what the host's own client uses.
//
// JOINING A FRIEND
//   The host publishes a rich-presence "connect" string (publish_join_info). When a friend clicks "Join Game" in
//   the Steam overlay/friends list, Steam delivers it (running game: callback; not running: launch argument
//   `+connect_steam <steamid64>`). take_join_request() returns the host SteamID to connect to.
//
// Compiled without the SDK (T3D_STEAM undefined) every call is a harmless no-op and init() returns false.

#include <cstdint>
#include <memory>
#include <optional>
#include <string>

namespace t3d::net
{
    class SteamPlatform
    {
      public:
        SteamPlatform();
        ~SteamPlatform();
        SteamPlatform(SteamPlatform const &) = delete;
        auto operator=(SteamPlatform const &) -> SteamPlatform & = delete;

        // True if this build contains Steamworks support.
        static auto compiled_in() -> bool;
        // True while a SteamPlatform in this process is initialised. Steam transports require it.
        static auto available() -> bool;

        auto init() -> bool;
        void shutdown();
        // Pumps SteamAPI callbacks. Call once per frame, from the main thread.
        void update();

        [[nodiscard]] auto last_error() const -> std::string const & { return error_; }
        [[nodiscard]] auto local_id() const -> std::uint64_t;       // SteamID64 of the logged-in user (0 if not ready)
        [[nodiscard]] auto local_name() const -> std::string;       // persona name
        [[nodiscard]] auto relay_status() const -> std::string;     // "ready", "attempting", ... for the debug UI

        // Advertise (hosting = true) or withdraw our session so friends can click "Join Game".
        void publish_join_info(bool hosting);
        // Parses `+connect_steam <steamid64>` from the command line (call once after init()).
        void parse_command_line(int argc, char const * const * argv);
        // Host SteamID requested via overlay/invite/command line, if any. Consumed on read.
        [[nodiscard]] auto take_join_request() -> std::optional<std::uint64_t>;

      private:
        struct Impl;
        std::unique_ptr<Impl> impl_;
        std::string error_;
    };
} // namespace t3d::net
