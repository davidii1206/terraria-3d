#pragma once
// Mobs (hostile, passive) and NPCs (town, vendors) - server-side AI.
//
// MobDef (data/mobs/*.json): name, model, size, health, damage, defense, speed, drops (loot table),
//   spawn rules (biome, layer, time of day, light level, min/max group, rarity, event/boss flag),
//   behaviours list, sounds, knockback resistance, flying/swimming/burrowing flags.
//
// AI building blocks (compose per mob; start with a simple state machine, move to behaviour trees
// only if needed):
//   Idle/Wander, Chase target (nearest player in aggro range + line of sight), Melee attack with cooldown,
//   Ranged attack (shoot projectile), Flee, Jump over 1-block obstacles, Climb, Swim, Fly (Terraria's
//   bats, demon eyes, worms), Burrow (worms tunnel through blocks!), Teleport, Summon.
// Pathfinding in 3D: A* on the voxel grid with a node budget per tick, cached paths, only for mobs
//   that need it; most Terraria-style mobs can steer directly toward the target and jump.
//
// Spawning (world/ + game/): per player, around them but outside view and inside a ring, with
//   a global cap per player and per biome. Despawn when far from all players. Day/night, depth layer,
//   biome, blood-moon/invasion events change the spawn tables.
//
// Town NPCs (Terraria): move into a valid "house" (enclosed room with light, door, furniture), sell items,
//   have happiness/price rules, can be killed by invasions. Need a housing validity check (flood fill) - game/.
//
// Bosses: special mobs with phases, arena rules, boss bar, summon items, drop progression items.
// Implemented as a mob with a scripted phase state machine + event hooks (game/events.hpp).

namespace t3d
{
    // TODO: MobDef, MobRegistry, AI components/systems, SpawnSystem, NpcHousing.
} // namespace t3d
