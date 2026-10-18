#pragma once
// Minimal logging facade. Swap the body for spdlog/fmt later without touching call sites.
// Planned: levels (trace/info/warn/error), per-subsystem tags ("net", "gen", "render"),
// log to stdout + rotating file, and a ring buffer that the in-game console can display.

#include <iostream>

namespace t3d
{
    // TODO: replace with a real logger. Kept as macros-free inline helpers on purpose.
    inline void log_info(char const * msg) { std::cout << "[info] " << msg << '\n'; }
    inline void log_error(char const * msg) { std::cerr << "[error] " << msg << '\n'; }
} // namespace t3d
