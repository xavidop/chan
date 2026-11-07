#pragma once

#include <chan/core/config.h>

namespace chan {

// Sentinel for "the local time is not known yet" (clock not set by SNTP).
constexpr int kUnknownMinuteOfDay = -1;

// True once the system clock holds a plausible date; before SNTP syncs it reads 1970.
bool clock_is_valid(int year);

// True when `minute_of_day` (0..1439, local time) falls inside the quiet window.
// Windows may wrap midnight (e.g. 23:00–07:00). The end minute is exclusive.
// kUnknownMinuteOfDay (or any negative value) is never quiet: without a valid clock the
// device must not mute itself.
bool in_quiet_hours(const QuietHours& q, int minute_of_day);

}  // namespace chan
