#pragma once
// First-person camera (3D Terraria is played from the player's eyes; a third-person /
// free-fly debug camera can reuse the same struct).
//
// Responsibilities:
//  - view / projection matrices (GLM, reversed-Z recommended for precision over large worlds;
//    CMake already defines GLM_FORCE_DEPTH_ZERO_TO_ONE for Vulkan)
//  - camera-relative rendering: subtract the camera position (in double) from world positions
//    before upload, so floats stay precise far from the origin (worlds are thousands of blocks wide)
//  - frustum extraction for chunk/entity culling (6 planes) + AABB-vs-frustum test
//  - smooth look, view bobbing, FOV change while sprinting, underwater flag (eye inside liquid)

#include "core/types.hpp"

namespace t3d
{
    struct Camera
    {
        glm::dvec3 position = {0, 0, 0}; // double: see camera-relative rendering above
        f32 yaw = 0, pitch = 0;
        f32 fov_degrees = 80.0f;
        f32 near_plane = 0.1f, far_plane = 2000.0f;

        // TODO: view_matrix(), projection_matrix(aspect), frustum(), forward()/right()/up().
    };
} // namespace t3d
