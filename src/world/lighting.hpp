#pragma once
// Voxel lighting: two channels per voxel, 0..15 each.
//  * SKY light: 15 where the column is open to the sky; propagates sideways/down with -1 per step
//    (full strength straight down through air = "sunbeams" into caves and shafts).
//  * BLOCK light: emitted by torches, lava, glowing ores, etc.; flood-fill with -1 per step.
//
// Algorithm: BFS flood-fill queues for light ADD and light REMOVE (the classic Minecraft approach).
//  - Chunk gen: initialise sky light from the heightmap, then propagate across chunk borders.
//  - Block placed/removed: incremental update touching only the affected region (may cross chunks;
//    lock/queue per chunk, process on a worker, then mark neighbours for remesh).
//  - Day/night: store sky light at full brightness and multiply by a time-of-day factor in the
//    shader - don't recompute lighting when the sun moves.
// Rendering: values go into mesher vertices (smooth lighting) - see render/chunk_mesher.hpp.
// Network: clients can recompute light locally from the chunk data (cheaper than sending it),
// as long as the algorithm is deterministic. Decide on this when writing the protocol.

namespace t3d
{
    class LightEngine
    {
      public:
        // TODO: void on_block_changed(World&, BlockPos, BlockId old_id, BlockId new_id);
        //       void init_chunk(World&, Chunk&);
    };
} // namespace t3d
