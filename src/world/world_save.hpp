#pragma once
// Persistence (server only; the client only saves settings and maybe a server list).
//
// Layout on disk:  saves/<world>/
//    level.dat            WorldInfo (seed, size, time, spawn, progression flags, block name table, version)
//    regions/r.X.Z.bin    a region = 8x8 columns of chunks (all Y chunks of a column stored together):
//                         header (offset/size/timestamp table) + compressed chunk blobs (zstd or lz4).
//                         Region files avoid millions of tiny files and keep related chunks together.
//    players/<uuid>.dat   inventory, position, health, spawn, stats, per-world map exploration
//    entities/            persistent mobs/NPCs/dropped items (or inside chunk blobs)
//
// Rules
//  - Version every format; write migration steps rather than breaking old worlds.
//  - Block ids are remapped through the name table in level.dat (see core/types.hpp).
//  - Saves are async: serialize on the game thread (cheap copy), compress+write on a worker.
//    Write to a temp file then rename (atomic) to avoid corruption on crash. Keep a .bak of level.dat.
//  - Autosave every N minutes + on shutdown + when a chunk unloads dirty. Never block the tick.
//  - World folder lock file so two servers can't open the same world.

namespace t3d
{
    class WorldSave
    {
      public:
        // TODO: open/create(path); load_chunk(ChunkPos) -> optional<Chunk>; save_chunk(Chunk const&);
        //       load_player(uuid)/save_player; flush(); close().
    };
} // namespace t3d
