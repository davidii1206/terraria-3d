#include "render/renderer.hpp"
#include "core/log.hpp"
#include "daxa/c/core.h"
#include "daxa/sync.hpp"
#include "daxa/types.hpp"
#include "daxa/utils/pipeline_manager.hpp"
#include "platform/window.hpp"
#include "shared.inl"

namespace t3d
{
    Renderer::Renderer(Window & window)
        : window_{window},
          instance_{daxa::create_instance({})},
          device_{instance_.create_device_2(instance_.choose_device({}, {}))},
          swapchain_{device_.create_swapchain({
              .native_window_info = window.native_info(),
              .surface_format = device_.choose_swapchain_surface_format({.native_window_info = window.native_info()}),
              .present_mode = daxa::PresentMode::FIFO, // TODO: expose vsync (MAILBOX/IMMEDIATE)
              .image_usage = daxa::ImageUsageFlagBits::TRANSFER_DST,
              .name = "swapchain",
          })},
          pipeline_manager_{daxa::PipelineManager({
              .device = device_,
              .root_paths = {DAXA_SHADER_INCLUDE_DIR, SHADER_DIR},
              .default_language = daxa::ShaderLanguage::GLSL,
              .default_enable_debug_info = true,
              .name = "pipeline manager",
          })}
    {
        // create the render_target
        create_render_target();

        compute_pipeline_ = pipeline_manager_.add_compute_pipeline2({
            .source = daxa::ShaderFile{"raymarch.glsl"},
            .entry_point = "main"
        }).value();
    }

    Renderer::~Renderer() {
        device_.destroy_image(render_target_);
    }

    void Renderer::render(RenderView const & view)
    {
        if (window_.minimized()) { return; }

        if (window_.resized)
        {
            swapchain_.resize();
            device_.destroy_image(render_target_);
            create_render_target();
            window_.resized = false;
        }
        
        daxa::ImageId swapchain_image = swapchain_.acquire_next_image();
        if (swapchain_image.is_empty()) { return; }

        daxa::CommandRecorder recorder = device_.create_command_recorder({.name = "main cmd recorder"});
        daxa::ImageInfo image_info = device_.image_info(swapchain_image).value();

        recorder.pipeline_image_barrier({
            .src_access = daxa::AccessConsts::COMPUTE_SHADER_WRITE, 
            .image = render_target_,
            .layout_operation = daxa::ImageLayoutOperation::TO_GENERAL
        });

        // dispatch compute shader
        // no player position yet
        recorder.set_pipeline(*compute_pipeline_);
        recorder.push_constant(RaymarchPush{
            .image_id = render_target_.default_view(),
            .frame_dim = {window_.width(), window_.height()},
            .cam_pos     = to_daxa(glm::vec3(view.camera.position)),   // dvec3 -> vec3 first
            .cam_forward = to_daxa(view.camera.forward()),
            .cam_right   = to_daxa(view.camera.right()),
            .cam_up      = to_daxa(view.camera.up()),
            .tan_half_fov = std::tan(glm::radians(view.camera.fov_degrees) * 0.5f),
        });
        recorder.dispatch({(window_.width() + 7) / 8, (window_.height() + 7) / 8});

        // render_target transfer read
        recorder.pipeline_image_barrier({
            .src_access = daxa::AccessConsts::COMPUTE_SHADER_WRITE,
            .dst_access = daxa::AccessConsts::BLIT_READ,
            .image = render_target_,
            .layout_operation = daxa::ImageLayoutOperation::TO_GENERAL
        });

        recorder.pipeline_image_barrier({
            .dst_access = daxa::AccessConsts::BLIT_WRITE,
            .image = swapchain_image,
            .layout_operation = daxa::ImageLayoutOperation::TO_GENERAL
        });

        // TODO: scaling
        recorder.blit_image_to_image({
            .src_image = render_target_,
            .dst_image = swapchain_image,
            .src_offsets = {{ {0, 0, 0}, {static_cast<i32>(device_.image_info(render_target_).value().size.x), static_cast<i32>(device_.image_info(render_target_).value().size.y), 1} }},
            .dst_offsets = {{ {0, 0, 0}, {static_cast<i32>(image_info.size.x), static_cast<i32>(image_info.size.y), 1} }},
            .filter = daxa::Filter::LINEAR
        });

        recorder.pipeline_image_barrier({
            .src_access = daxa::AccessConsts::BLIT_WRITE,
            .image = swapchain_image,
            .layout_operation = daxa::ImageLayoutOperation::TO_PRESENT_SRC,
        });

        daxa::ExecutableCommandList cmd_list = recorder.complete_current_commands();
        device_.submit_commands({
            .command_lists = std::array{cmd_list},
            .wait_binary_semaphores = std::array{swapchain_.current_acquire_semaphore()},
            .signal_binary_semaphores = std::array{swapchain_.current_present_semaphore()},
            .signal_timeline_semaphores = std::array{swapchain_.current_timeline_pair()},
        });
        device_.present_frame({
            .wait_binary_semaphores = std::array{swapchain_.current_present_semaphore()},
            .swapchain = swapchain_,
        });
        device_.collect_garbage();
    }

    void Renderer::create_render_target() {
        // TODO: window size * scale
        render_target_ = device_.create_image({
            .format = daxa::Format::R16G16B16A16_SFLOAT,
            .size = {window_.width(), window_.height(), 1},
            .usage = daxa::ImageUsageFlagBits::SHADER_STORAGE | daxa::ImageUsageFlagBits::TRANSFER_SRC,
            .name = "render_target_image"
        });
    }
} // namespace t3d
