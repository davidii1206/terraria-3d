#pragma once
// User interface.
//
// Two layers:
//  1. Dear ImGui (already built via Daxa: DAXA_ENABLE_UTILS_IMGUI) - debug overlay and dev tools:
//     F3-style info (fps, frame graph, position, chunk counts, mesh queue, ping, loaded entities), chunk borders,
//     wireframe, free-cam, world editor / block picker, network stats, live settings.
//  2. Game UI - custom-drawn (textured quads / ImGui draw list with a custom skin; pick one early):
//     - HUD: crosshair, hotbar, health/mana bars, buff icons, boss bar, minimap, damage numbers, chat feed
//     - Screens: main menu (singleplayer / join / host / settings / quit), world select + world creation
//       (size, seed, difficulty), server browser / direct connect, pause menu, settings (video/audio/controls),
//       inventory + crafting, chest/furnace/shop screens, map screen, death screen, loading screen
//     - Chat box with command autocomplete, tooltips for items (stats, prefix, lore)
//  Screen stack with push/pop; the top screen decides if the world receives input or the cursor is captured.
//  Localisation: all strings through a key->text table (data/lang/en.json) from the start.
//  Scaling: UI scale setting + DPI aware; layout in "virtual pixels".

namespace t3d
{
    // TODO: ScreenStack, Screen base class, HUD, widgets, font loading (+ icons from the texture array).
} // namespace t3d
