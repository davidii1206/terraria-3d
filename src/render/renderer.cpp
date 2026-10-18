#include "render/renderer.hpp"

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
    }

    Renderer::~Renderer() = default; // Daxa waits for the GPU and collects garbage on device destruction.

    void Renderer::render(RenderView const &)
    {
        if (window_.resized)
        {
            swapchain_.resize();
            window_.resized = false;
        }
        if (window_.minimized()) { return; }

        daxa::ImageId swapchain_image = swapchain_.acquire_next_image();
        if (swapchain_image.is_empty()) { return; }

        daxa::CommandRecorder recorder = device_.create_command_recorder({.name = "main cmd recorder"});
        daxa::ImageInfo image_info = device_.image_info(swapchain_image).value();

        recorder.pipeline_image_barrier({
            .dst_access = daxa::AccessConsts::COLOR_ATTACHMENT_OUTPUT_READ_WRITE,
            .image = swapchain_image,
            .layout_operation = daxa::ImageLayoutOperation::TO_GENERAL,
        });

        // Placeholder pass: clear to sky blue. All real passes (see renderer.hpp) go in here.
        daxa::RenderCommandRecorder render_recorder = std::move(recorder).begin_renderpass({
            .color_attachments = std::array{
                daxa::RenderAttachmentInfo{
                    .image_view = swapchain_image.default_view(),
                    .load_op = daxa::AttachmentLoadOp::CLEAR,
                    .clear_value = std::array<daxa::f32, 4>{0.45f, 0.65f, 0.95f, 1.0f},
                },
            },
            .render_area = {.width = image_info.size.x, .height = image_info.size.y},
        });
        recorder = std::move(render_recorder).end_renderpass();

        recorder.pipeline_image_barrier({
            .src_access = daxa::AccessConsts::COLOR_ATTACHMENT_OUTPUT_READ_WRITE,
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
} // namespace t3d
