#pragma once
// Shared primitive types, ids and world-size constants used by every module.
// Keep this header tiny and dependency-free (only <cstdint> + GLM) because everything includes it.

#include <cstdint>
#include <glm/glm.hpp>

namespace t3d
{
    using u8 = std::uint8_t;
    using u16 = std::uint16_t;
    using u32 = std::uint32_t;
    using u64 = std::uint64_t;
    using i32 = std::int32_t;
    using i64 = std::int64_t;
    using f32 = float;
    using f64 = double;

    // Ids are small integers so they can be sent over the network and stored on disk cheaply.
    // The *string* name (e.g. "terraria:dirt") is the stable identity in save files / mods;
    // numeric ids are assigned at startup by the registries and must never be saved raw
    // (save files store a name->id table in their header and remap on load).
    using BlockId = u16;   // 0 = air
    using ItemId = u16;    // 0 = empty slot
    using EntityId = u32;  // unique per world session, assigned by the server
    using PlayerId = u16;  // slot in the connected-player table
    using Tick = u64;      // server simulation tick counter

    using BlockPos = glm::ivec3; // absolute voxel coordinate. +Y is up. X/Z are horizontal.
    using ChunkPos = glm::ivec3; // chunk coordinate = floor(BlockPos / CHUNK_SIZE)

    // --- World geometry -------------------------------------------------------------------
    // Terraria is a wide, deep world made of vertical "layers"; the 3D version keeps that:
    // the world is a bounded box, much wider than it is tall, split into depth layers
    // (space / sky / surface / underground / caverns / underworld, see world/biome.hpp).
    inline constexpr int CHUNK_SIZE = 32;                                   // voxels per chunk edge
    inline constexpr int CHUNK_VOLUME = CHUNK_SIZE * CHUNK_SIZE * CHUNK_SIZE;
    inline constexpr int WORLD_HEIGHT_CHUNKS = 16;                          // 512 blocks tall
    // Horizontal size is chosen per world (small/medium/large) and stored in WorldInfo.

    // --- Simulation timing -----------------------------------------------------------------
    inline constexpr int SERVER_TICK_RATE = 30;                             // Hz, fixed timestep
    inline constexpr f64 SERVER_TICK_DT = 1.0 / SERVER_TICK_RATE;
} // namespace t3d
