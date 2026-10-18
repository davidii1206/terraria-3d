#include "platform/window.hpp"

#include <GLFW/glfw3.h>
#if defined(_WIN32)
#define GLFW_EXPOSE_NATIVE_WIN32
#elif defined(__linux__)
#define GLFW_EXPOSE_NATIVE_X11
#define GLFW_EXPOSE_NATIVE_WAYLAND
#endif
#include <GLFW/glfw3native.h>

namespace t3d
{
    Window::Window(char const * title, u32 width, u32 height) : width_{width}, height_{height}
    {
        glfwInit();
        glfwWindowHint(GLFW_CLIENT_API, GLFW_NO_API); // Vulkan: no GL context
        window_ = glfwCreateWindow(static_cast<int>(width), static_cast<int>(height), title, nullptr, nullptr);
        glfwSetWindowUserPointer(window_, this);
        glfwSetFramebufferSizeCallback(
            window_,
            [](GLFWwindow * w, int new_width, int new_height)
            {
                auto & self = *static_cast<Window *>(glfwGetWindowUserPointer(w));
                self.width_ = static_cast<u32>(new_width);
                self.height_ = static_cast<u32>(new_height);
                self.resized = true;
            });
    }

    Window::~Window()
    {
        glfwDestroyWindow(window_);
        glfwTerminate();
    }

    void Window::poll_events() { glfwPollEvents(); }

    auto Window::should_close() const -> bool { return glfwWindowShouldClose(window_) != 0; }

    auto Window::native_info() const -> daxa::NativeWindowInfo
    {
#if defined(_WIN32)
        return daxa::NativeWindowInfoWin32{glfwGetWin32Window(window_)};
#elif defined(__linux__)
        if (glfwGetPlatform() == GLFW_PLATFORM_WAYLAND)
        {
            return daxa::NativeWindowInfoWayland{
                .display = glfwGetWaylandDisplay(),
                .surface = glfwGetWaylandWindow(window_),
                .width = width_,
                .height = height_,
            };
        }
        return daxa::NativeWindowInfoXlib{.window = reinterpret_cast<void *>(glfwGetX11Window(window_))};
#endif
    }
} // namespace t3d
