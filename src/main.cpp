// terraria-3d - a Terraria-inspired multiplayer sandbox in 3D, built on Daxa (Vulkan).
//
// SOURCE LAYOUT (each header explains its module's design in comments)
//   core/      types, config, logging, job system, App (run modes + main loop)
//   platform/  window (GLFW), input
//   render/    Daxa renderer, camera, chunk mesher + GPU chunk storage, texture array
//   world/     blocks, chunks, world container, world generation, biomes, lighting, fluids, saving
//   physics/   AABB collision, voxel/entity raycasts (shared by server and client prediction)
//   entity/    ECS components/systems, players, mobs/NPCs/bosses
//   game/      items, inventory, crafting, combat, events/progression/commands
//   net/       transport, protocol, replication, server, client
//   ui/        HUD, menus, debug overlay          audio/  music + 3D sound
//   data/      JSON content (blocks, items, recipes, mobs...)   shaders/  GLSL    assets/  textures, models, sounds
//
// CORE DESIGN DECISIONS
//   * Server-authoritative multiplayer; singleplayer = integrated server over a loopback transport.
//   * Fixed-timestep server simulation, variable-timestep client with prediction + interpolation.
//   * Content is data-driven (JSON registries), numeric ids are runtime-only, names are the stable identity.
//   * World = 32^3 chunks in a bounded box; wide and vertically layered like Terraria
//     (sky, surface, underground, caverns, underworld).
//   * Heavy work (generation, meshing, lighting, IO) runs on worker threads; results are applied on the owner thread.

#include "core/app.hpp"

#include <string_view>

auto main(int argc, char ** argv) -> int
{
    t3d::AppArgs args;
    for (int i = 1; i < argc; ++i)
    {
        std::string_view const arg = argv[i];
        if (arg == "--server") { args.mode = t3d::RunMode::DedicatedServer; }
        else if (arg == "--client") { args.mode = t3d::RunMode::Client; }
        // TODO: --connect <host:port>, --port, --world, --seed, --name, --render-distance ...
    }
    return t3d::App{std::move(args)}.run();
}
