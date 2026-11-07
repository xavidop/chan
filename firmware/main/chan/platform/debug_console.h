// Tiny line-oriented debug console on the USB Serial/JTAG port (the same port that
// carries the logs). Type `help` in `chan.py monitor`. Development aid only.
#pragma once

#include <cstddef>
#include <functional>
#include <string_view>

namespace chan::platform {

// Starts the reader task; `handler` is called from that task with each trimmed line.
void debug_console_start(std::function<void(std::string_view line)> handler);

// Minimum free stack the reader task has ever had, in bytes (0 before it starts).
size_t debug_console_stack_free_bytes();

}  // namespace chan::platform
