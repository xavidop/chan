#pragma once

#include <chan/core/config.h>

namespace chan {

// True when `minute_of_day` (0..1439, local time) falls inside the quiet window.
// Windows may wrap midnight (e.g. 23:00–07:00). The end minute is exclusive.
bool in_quiet_hours(const QuietHours& q, int minute_of_day);

}  // namespace chan
