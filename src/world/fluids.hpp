#pragma once
// Liquids (water, lava, honey...) - a Terraria signature and the hardest simulation to get right.
//
// Model: each liquid voxel stores a level 0..8 (0 = source/full, or Terraria-like fill amount) in the block 'state'.
// Rules per scheduled tick for one liquid voxel:
//   1. If the block below is not solid -> flow down (move/copy amount downward first).
//   2. Else spread horizontally to the 4 neighbours, equalising levels.
//   3. Water + lava interaction -> obsidian/stone; lava ignites flammables; water extinguishes torches.
//   4. Optionally "pressure": a full column pushes liquid up through connected tubes.
// Performance: only voxels that changed are put on the scheduled-tick queue (not a full-world scan);
// cap updates per tick and per chunk; settle (stop ticking) when stable. Server-only simulation;
// clients just receive the resulting block changes (batched), and the shader animates the surface.
// Rendering: liquid surface height from the level; separate transparent mesh layer.
// Gameplay: swimming/drowning, buoyancy, lava damage, buckets, pumps (later).

namespace t3d
{
    enum class LiquidType { None, Water, Lava, Honey };

    class FluidSimulator
    {
      public:
        // TODO: void schedule(BlockPos); void tick(World&, int max_updates);
    };
} // namespace t3d
