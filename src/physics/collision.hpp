#pragma once
// Entity-vs-voxel collision (server authoritative, client predicts with the SAME code - keep this
// module deterministic and free of any rendering/network includes so both sides share it).
//
// Approach (standard for voxel games):
//  - Entity = AABB (players ~0.6 x 1.8 x 0.6 blocks), position + velocity.
//  - Each tick: apply gravity/drag/input accel, then move axis by axis (Y, then X, then Z):
//    gather solid block boxes overlapping the swept AABB, clip the movement against them.
//  - Step-up (auto-climb 1 block when sprinting/walking), sneak edge-safety, ground flag, ceiling hit.
//  - Liquids: swim state when submerged, reduced gravity/speed; ladders/ropes/vines climb state.
//  - Ground friction by block type (ice slippery, honey slow), fall damage from fall distance.
//  - Non-full block shapes use their collision boxes from BlockDef (slabs, stairs, platforms
//    you can jump through - Terraria platforms).
//  - Entity-vs-entity: push/overlap queries via a spatial hash grid (also used for melee hits,
//    projectiles and "who is near this player" network relevance).
//  - Use fixed-timestep + f32 or f64? Use f64 for positions on the server, f32 is fine for velocities.

#include "core/types.hpp"

namespace t3d
{
    struct AABB
    {
        glm::dvec3 min, max;
    };

    // TODO: void move_and_collide(World const&, AABB&, glm::dvec3& velocity, glm::dvec3 delta, CollisionResult&);
} // namespace t3d
