#pragma once
// Runtime configuration. Two kinds, deliberately separate:
//
//  * ClientSettings  - per-user, saved to a settings file (render distance, vsync, fov, mouse
//                      sensitivity, key bindings, audio volumes, username, UI scale).
//  * ServerSettings  - per-world/host (port, max players, tick rate, difficulty, world seed,
//                      world size, pvp on/off, whitelist, MOTD, autosave interval).
//
// Format: JSON or TOML in the user config dir (XDG on Linux). Load at startup, write on change.
// Command line flags override the file (see main.cpp).

#include "core/types.hpp"
#include <string>

namespace t3d
{
    struct ClientSettings
    {
        std::string username = "Player";
        u32 render_distance_chunks = 12;
        f32 fov_degrees = 80.0f;
        bool vsync = true;
        f32 mouse_sensitivity = 1.0f;
        // TODO: key bindings map, master/music/sfx volumes, UI scale, fullscreen, gui toggles.
    };

    struct ServerSettings
    {
        u16 port = 25565;
        u16 max_players = 8;
        u64 world_seed = 0;
        std::string world_name = "world";
        // TODO: world size preset, difficulty (journey/classic/expert/master like Terraria),
        //       pvp, autosave interval, view distance cap, whitelist/ops files, MOTD.
    };
} // namespace t3d
