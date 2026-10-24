#pragma once
// Minimal logging facade. Swap the body for spdlog later without touching call sites.
// Planned: levels (trace/info/warn/error), per-subsystem tags ("net", "gen", "render"),
// log to stdout + rotating file, and a ring buffer that the in-game console can display.

#include <format>
#include <iostream>

namespace t3d
{
    template <typename... Args>
    void log_info(std::format_string<Args...> fmt, Args &&... args)
    {
        std::cout << "[info] " << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }

    template <typename... Args>
    void log_warn(std::format_string<Args...> fmt, Args &&... args)
    {
        std::cerr << "[warn] " << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }

    template <typename... Args>
    void log_error(std::format_string<Args...> fmt, Args &&... args)
    {
        std::cerr << "[error] " << std::format(fmt, std::forward<Args>(args)...) << '\n';
    }
} // namespace t3d
