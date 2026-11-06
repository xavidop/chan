// Renders an Attention Output onto the 12 body LEDs (6 left + 6 right).
#pragma once

#include <chan/core/attention.h>

#include <cstdint>

namespace chan::platform {

// Writes one frame for the pattern in `out` at time `now_ms`. `voice_level` is 0..1 for
// VoiceLevel (anything < 0 means "unknown": animate a gentle pulse instead).
// Skips the write when nothing changed since the previous frame.
void leds_apply(const Output& out, uint32_t now_ms, float voice_level);

}  // namespace chan::platform
