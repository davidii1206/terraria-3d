#pragma once
// Types shared between C++ and GLSL (included from both). Add them as the renderer grows:
//
//   struct GlobalUniforms { view_proj, camera_pos, sun_dir, sun_color, ambient, fog_*, time, ... }
//   struct ChunkDrawInfo  { chunk origin, vertex offset/count per layer }
//   struct EntityInstance { transform, tint, skeleton offset, texture layer }
//   struct TerrainPush    { daxa_BufferPtr(GlobalUniforms) globals; daxa_BufferPtr(ChunkDrawInfo) chunks; ... }
//
// Planned shader files (compiled at runtime by daxa::PipelineManager):
//   terrain.glsl   unpack vertex, sample texture array, apply AO + block/sky light, fog
//   entity.glsl    skinned mesh rendering
//   sky.glsl       gradient, sun/moon discs, stars, clouds
//   water.glsl     transparent liquid with animated normals and depth fade
//   cull.glsl      compute: chunk frustum/occlusion culling -> indirect draw buffer
//   shadow.glsl    sun shadow cascades
//   post.glsl      fog, underwater, tonemap
#include <daxa/daxa.inl>

struct RaymarchPush {
    daxa_ImageViewId image_id;
    daxa_u32vec2 frame_dim;
    daxa_f32vec3 cam_pos;
    daxa_f32vec3 cam_forward;
    daxa_f32vec3 cam_right;
    daxa_f32vec3 cam_up;
    daxa_f32     tan_half_fov;
};