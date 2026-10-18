#pragma once
// Game-wide systems that make it feel like Terraria rather than just "Minecraft with caves".
//
//  * Day/night cycle: the world clock lives in World; this module reacts to time of day
//    (mob spawn tables, NPC behaviour, music) and moon phases.
//  * Weather: rain/snow/sandstorm/thunder per biome region, synced as a small global state + seed.
//  * Random events and invasions: blood moon, goblin-army-like invasions, slime rain, solar eclipse.
//    Event = state machine {trigger condition, wave list with mob groups, progress bar, win/lose, rewards}.
//  * Bosses: summon conditions, arena handling, despawn rules, progression flags set on defeat.
//  * Progression flags (stored in WorldInfo): e.g. defeated_boss_1, hardmode_started. Flags unlock new
//    ore generation, new spawn tables, new recipes, new NPCs. Make flags data-driven (JSON) so content
//    designers add progression without touching C++.
//  * Housing/NPC arrival conditions.
//  * Achievements/stats (optional), map exploration data (per player), fishing, pets, mounts, grappling hook
//    (movement mechanics that need server support in physics/), minecart tracks, wiring.
//  * Game modes: survival, creative/journey (item duplication, free build), spectator, hardcore.
//  * Commands: chat commands (/tp, /give, /time, /gamemode, /kick ...) with permission levels,
//    shared by the server console, RCON-like admin tools and in-game chat. Registered via a small command registry.

namespace t3d
{
    // TODO: EventManager, Weather, ProgressionFlags, CommandRegistry.
} // namespace t3d
