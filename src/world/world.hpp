#pragma once
// World = the authoritative container of all chunks + the rules for loading/unloading them.
// The SERVER owns the real World. The CLIENT keeps a World-shaped cache filled by chunk packets
// (same class, no generation, no authority).
//
// Responsibilities
//  - Chunk map: unordered_map<ChunkPos, unique_ptr<Chunk>> (custom hash for ivec3).
//  - get_block / set_block (world coords). set_block marks the chunk (and neighbour chunks when the
//    block touches a border) for remesh, lighting update, network broadcast and saving.
//  - Chunk lifecycle driven by "tickets": every player holds a ticket radius; spawn area keeps a
//    permanent one. Chunks with no ticket are saved and unloaded after a grace period.
//    Missing chunks are requested from WorldGenerator (new) or WorldSave (existing) on workers.
//  - World bounds: clamp/deny edits outside the finite world; solid "world edge" so players can't
//    fall out. Y below 0 = bedrock/underworld floor; above the build limit = space.
//  - World time: day/night cycle (tick of day), moon phase, day count - drives sun, spawning, events.
//  - Block updates scheduler: scheduled ticks (liquid flow, falling sand, redstone-like wiring)
//    and random ticks per loaded chunk (grass, crops, decay). Bounded work per tick!
//  - Queries used by everything else: raycast (physics/raycast.hpp), collision boxes in a region,
//    highest solid block in a column (spawning, surface lookup), is_loaded(pos).
//  - Events/hooks: on_block_changed(callback) so lighting, fluids, networking and AI pathfinding
//    caches react without World knowing about them.
//
// WorldInfo (stored in level file): seed, size, name, creation time, game version, spawn point,
// world time, difficulty, flags (hardmode-like progression flags, defeated bosses), block-name->id table.

#include "core/types.hpp"

namespace t3d
{
    class World
    {
      public:
        // TODO: see responsibilities above.
    };
} // namespace t3d
