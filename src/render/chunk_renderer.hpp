#pragma once
// GPU-side storage and drawing of all chunk meshes.
//
// Strategy (scales to thousands of chunks with very few draw calls):
//  - One (or a few) huge vertex buffers, sub-allocated with a free-list/TLSF allocator.
//    Each chunk mesh = (offset, count) into it. Freed when the chunk unloads or is remeshed.
//  - A per-chunk "draw info" SSBO: chunk origin, vertex range, layer ranges.
//  - Culling on the GPU (compute): frustum + optional Hi-Z occlusion test writes a compacted
//    indirect draw buffer; then vkCmdDrawIndirectCount. Start with CPU frustum culling and
//    plain indirect draws, move to GPU culling when chunk counts demand it.
//  - Caves matter in Terraria-like games: a CPU "cave culling" visibility graph (flood fill
//    through chunk faces, like Minecraft's) avoids drawing everything behind solid rock.
//  - Upload through a staging ring buffer; limit uploads per frame to avoid hitches.
//  - Draw order: opaque -> cutout -> sky -> transparent(far-to-near by chunk).

namespace t3d
{
    class ChunkRenderer
    {
      public:
        // TODO: void upload(ChunkPos, ChunkMeshData&&);   void remove(ChunkPos);
        //       void cull(Camera const&);                  void draw(/* command recorder */);
        //       stats(): chunks drawn, triangles, VRAM used.
    };
} // namespace t3d
