#pragma once
// Ray queries.
//
//  * Voxel raycast (Amanatides & Woo DDA): from the eye along the view direction up to reach distance
//    (~5 blocks, longer for tools/grapple); returns first hit block position, face normal, hit point.
//    Respects non-full shapes (test the ray against the block's boxes). Optionally hits liquids.
//    Used for: mining, placing, block outline, interaction, line-of-sight for AI, lighting probes.
//  * Entity raycast: ray vs AABB of entities within reach, combined with the block ray so you
//    can't hit mobs through walls. Used for melee, bows, hitscan weapons.
//  * Projectile sweeps: capsule/AABB swept along the velocity each tick (arrows, thrown items, spells).
// All run on the server for validation; the client runs the same code for the outline and instant feedback.

#include "core/types.hpp"

namespace t3d
{
    struct BlockHit
    {
        bool hit = false;
        BlockPos block = {0, 0, 0};
        glm::ivec3 face_normal = {0, 0, 0};
        f32 distance = 0;
    };
    // TODO: BlockHit raycast_blocks(World const&, glm::dvec3 origin, glm::dvec3 dir, f64 max_dist);
} // namespace t3d
