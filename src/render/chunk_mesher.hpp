#pragma once
// Turns voxel data into triangles. Runs on worker threads (core/job_system.hpp), never on
// the render thread. Input is a *snapshot* of the chunk plus a 1-block border copied from
// its 6 (or 26, for AO at corners) neighbours so the job needs no locks while running.
//
// Pipeline:
//  1. Cull hidden faces: emit a face only if the neighbouring voxel is transparent or a different
//     "render layer" (so water next to glass still shows).
//  2. Greedy meshing: merge coplanar faces with identical texture + light + AO into big quads.
//     (Binary greedy meshing with bitmasks is ~10x faster than the naive version.)
//  3. Ambient occlusion: 0..3 per vertex from the 3 neighbours around each corner; flip the quad
//     diagonal when needed to avoid the anisotropy artefact.
//  4. Light: sample block + sky light (world/lighting.hpp) per vertex, smooth-lighting style.
//  5. Vertex packing: position (6 bits x3 within chunk), face id, texture layer, AO, light in a
//     single u32 or two u32s; the vertex shader unpacks. Index quads via a shared index buffer.
//  6. Separate output buffers per render layer: OPAQUE, CUTOUT (leaves, plants), TRANSPARENT
//     (water, glass, sorted later) and optionally non-cube "models" (stairs, torches, slabs,
//     fences) described in block JSON as small box lists.
//
// Output: ChunkMeshData { vectors of packed vertices per layer, bounding box }, handed to
// ChunkRenderer for upload.

namespace t3d
{
    struct ChunkMeshData
    {
        // TODO: std::vector<PackedVertex> opaque, cutout, transparent; AABB bounds; chunk version.
    };

    // TODO: auto mesh_chunk(ChunkSnapshot const&) -> ChunkMeshData;
} // namespace t3d
