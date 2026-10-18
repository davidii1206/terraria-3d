#pragma once
// A Chunk is a CHUNK_SIZE^3 cube of voxels.
//
// STORAGE
//  - Palette compression: each chunk keeps a small palette of the BlockIds it contains and a
//    bit-packed array of palette indices (1,2,4,8,16 bits per voxel as the palette grows).
//    Uniform chunks (all air - the sky - or all stone) store just one id: huge memory savings.
//  - Per-voxel 'state' nibble/byte for orientation, liquid level, growth stage (separate packed array,
//    allocated lazily only when needed).
//  - Light data: 4-bit block light + 4-bit sky light per voxel (lazily allocated).
//  - Background wall layer (Terraria-style): separate palette array, lazily allocated.
//  - Block entities (chests, furnaces, signs): small map<local pos, BlockEntity>.
//  - Biome data per column (or 4x4x4 cell) for tinting/spawning.
//
// STATE & THREADING
//  - ChunkStatus: EMPTY -> GENERATING -> GENERATED -> LIGHTING -> READY (+ SAVING/UNLOADING).
//  - 'version' counter bumps on every modification: the mesher/network/saver compare versions to
//    see if their work is stale.
//  - 'dirty' flags: needs_remesh, needs_save, needs_net_update (set of changed blocks since last send).
//  - Game thread owns mutation; worker threads only see immutable snapshots / copies.
//
// ADDRESSING
//  - World block pos -> chunk pos: floor division (watch negatives!) by CHUNK_SIZE.
//  - Local index: x + z*CHUNK_SIZE + y*CHUNK_SIZE*CHUNK_SIZE (Y-major keeps vertical columns
//    cache-friendly for lighting and gravity checks).

#include "core/types.hpp"

namespace t3d
{
    class Chunk
    {
      public:
        // TODO: BlockId get(local), void set(local, id, state), fill, is_uniform(), palette ops,
        //       serialize/deserialize (see world/world_save.hpp), snapshot() for the mesher.
    };
} // namespace t3d
