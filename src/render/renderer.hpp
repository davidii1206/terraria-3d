#pragma once
// Renderer: owns the Daxa instance/device/swapchain and draws one frame from a RenderView.
//
// ----------------------------------------------------------------------------------------
// FRAME OUTLINE (target, built up step by step)
//   1. acquire swapchain image
//   2. upload: new/changed chunk meshes, entity instance data, per-frame uniforms
//   3. shadow pass (cascaded shadow map for the sun; optional)
//   4. opaque terrain pass  - one multi-draw-indirect over all visible chunk meshes
//   5. entity pass          - players, mobs, item drops (glTF via cgltf, skinned later)
//   6. cutout pass          - leaves, grass blades, torches (alpha-tested)
//   7. sky pass             - sky dome/gradient, sun, moon, stars, clouds (drawn where depth==far)
//   8. transparent pass     - water/lava/glass, sorted back to front
//   9. post-process         - underwater tint, fog, bloom, tonemap (later)
//  10. UI pass              - ImGui (debug) + custom HUD (hotbar, health, inventory)
//  11. present
//
// Use daxa::TaskGraph (enable DAXA_ENABLE_UTILS_TASK_GRAPH in CMakeLists.txt) once there is
// more than one pass: it handles barriers and transient images, and every pass above becomes one task.
// Staging uploads: enable DAXA_ENABLE_UTILS_MEM (TransferMemoryPool / RingBuffer).
//
// Shaders live in shaders/ and are compiled at runtime by daxa::PipelineManager (hot reload
// on file save). Structs shared between C++ and GLSL go in shaders/shared.inl.
// ----------------------------------------------------------------------------------------

#include "daxa/gpu_resources.hpp"
#include "daxa/pipeline.hpp"
#include "platform/window.hpp"
#include "render/camera.hpp"
#include <memory>

#define SHADER_LANGUAGE DAXA_LANGUAGE_GLSL
#include <daxa/daxa.hpp>
#include <daxa/utils/pipeline_manager.hpp>

namespace
{
    auto to_daxa(glm::vec3 const & v) -> daxa_f32vec3 { return {v.x, v.y, v.z}; }
}

namespace t3d
{
    // Everything the renderer needs to know about the world for one frame. The game/client
    // fills this; the renderer never reaches into game state itself (keeps threads clean).
    struct RenderView
    {
        Camera camera;
        // TODO: sun direction + colour (from world time), sky colour, fog params,
        //       list of visible chunk meshes, entity render list, selection-box (block outline),
        //       break-progress overlay, debug flags, UI draw data.
    };

    class Renderer
    {
      public:
        explicit Renderer(Window & window);
        ~Renderer();

        // Draw one frame. Currently only clears the screen.
        void render(RenderView const & view);

        // TODO: chunk mesh upload/removal API (see render/chunk_renderer.hpp),
        //       texture atlas upload, screenshot, GPU timing queries, VRAM budget stats.

      private:
        Window & window_;
        daxa::Instance instance_;
        daxa::Device device_;
        daxa::Swapchain swapchain_;
        daxa::PipelineManager pipeline_manager_;
        std::shared_ptr<daxa::ComputePipeline> compute_pipeline_;
        daxa::ImageId render_target_;
        // TODO: depth image (recreated on resize), pipelines (terrain, entity, sky, water, ui),
        //       global uniform buffer, texture atlas image + sampler, ImGui renderer.

        // functions
        void create_render_target();
    };
} // namespace t3d
