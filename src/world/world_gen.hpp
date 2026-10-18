#pragma once
// Procedural world generation (server side, on worker threads, deterministic from the seed).
//
// Generation must be a PURE function of (seed, chunk position): any chunk can be generated
// independently and in any order. Features that cross chunk borders (trees, structures, ore
// veins, caves) therefore use either (a) deterministic per-region RNG so each chunk regenerates
// the parts of neighbouring features that overlap it, or (b) a multi-stage pipeline with
// a staged chunk status (see chunk.hpp) where decoration runs once all neighbours have terrain.
//
// Stages (Terraria-inspired layout, but in 3D):
//  1. Biome map: low-frequency 2D noise on X/Z -> forest, desert, snow, jungle, corruption/crimson,
//     ocean at world edges, etc. (world/biome.hpp). Blend borders so they don't cut hard.
//  2. Heightmap: surface height from layered noise (hills, mountains, valleys, flat spawn area).
//  3. Base fill: stone below the surface, biome top layers (grass+dirt, sand, snow) above;
//     bedrock / underworld floor at the bottom; underworld ("hell") layer with lava lakes.
//  4. Caves: 3D worms/"spaghetti" + "cheese" noise caves, bigger caverns deeper down; flooded caves
//     with water, lava below a given depth. Surface entrances so players can find them.
//  5. Ores: veins per depth band (copper/iron near surface, gold deeper, rare ores deepest),
//     each with min/max Y, vein size, frequency; progression gating like Terraria.
//  6. Liquids: seas, lakes, underground pools, settled once at generation time.
//  7. Decoration: trees (per biome), grass/flowers, cacti, mushrooms, glowing plants in caves.
//  8. Structures: villages / NPC houses, dungeons, underground cabins, temples, floating sky
//     islands, chests with loot tables. Placement via jittered grid per region (deterministic).
//  9. Spawn point: find a safe surface location near the world centre.
//
// Config: GenSettings (world size preset, sea level, cave density, ore richness, structure density) -
// part of WorldInfo so a server can recreate chunks consistently.
// Keep noise in a small library header (OpenSimplex/Perlin/FBM, domain warping) so it's reusable.

namespace t3d
{
    class WorldGenerator
    {
      public:
        // TODO: explicit WorldGenerator(u64 seed, GenSettings);
        //       void generate(Chunk&, ChunkPos);        // terrain + caves + ores
        //       void decorate(Chunk&, ChunkPos, neighbours);  // trees, structures
    };
} // namespace t3d
