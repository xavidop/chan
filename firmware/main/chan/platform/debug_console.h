// Tiny line-oriented debug console on the USB Serial/JTAG port (the same port that
// carries the logs). Type `help` in `chan.py monitor`. Development aid only.
#pragma once

#include <functional>
#include <string_view>

namespace chan::platform {

// Starts the reader task; `handler` is called from that task with each trimmed line.
void debug_console_start(std::function<void(std::string_view line)> handler);

}  // namespace chan::platform
