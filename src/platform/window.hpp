#pragma once
// Thin GLFW wrapper: owns the OS window, forwards events to Input, and hides the
// Wayland/X11/Win32 differences needed to create a Daxa swapchain.

#include "core/types.hpp"
#include <daxa/daxa.hpp>

struct GLFWwindow;

namespace t3d
{
    class Window
    {
      public:
        Window(char const * title, u32 width, u32 height);
        ~Window();
        Window(Window const &) = delete;
        auto operator=(Window const &) -> Window & = delete;

        void poll_events();
        [[nodiscard]] auto should_close() const -> bool;
        [[nodiscard]] auto native_info() const -> daxa::NativeWindowInfo;
        [[nodiscard]] auto width() const -> u32 { return width_; }
        [[nodiscard]] auto height() const -> u32 { return height_; }
        [[nodiscard]] auto minimized() const -> bool { return width_ == 0 || height_ == 0; }

        // Set by the framebuffer-size callback, cleared by whoever recreates the swapchain.
        bool resized = false;

        // TODO: set_mouse_captured(bool) for first-person look, fullscreen toggle,
        //       clipboard (chat paste), window icon, DPI scale, drop callbacks.
        // TODO: forward key/mouse/scroll/char callbacks into platform/input.hpp.

        [[nodiscard]] auto glfw() const -> GLFWwindow * { return window_; }

      private:
        GLFWwindow * window_ = nullptr;
        u32 width_ = 0, height_ = 0;
    };
} // namespace t3d
