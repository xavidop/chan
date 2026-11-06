// Maps Attention expressions onto the stock avatar (emotion + decorators) and motion.
// Call with the LVGL lock held.
#pragma once

#include <chan/core/attention.h>

namespace chan::platform {

// Applies `e` if it differs from the current one. Idle (Neutral) releases the avatar's
// modify lock so the stock idle-expression modifier takes over again.
void expressions_apply(Expression e);

// One-shot moves: the celebration dance (stock "Happy" keyframes) and a short turn to face
// the user (yaw 0, slight pitch up) used when an agent needs attention.
void expressions_dance();
void expressions_face_user();

}  // namespace chan::platform
