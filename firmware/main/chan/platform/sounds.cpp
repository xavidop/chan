#include "sounds.h"

#include <assets/assets.h>
#include <hal/board/hal_bridge.h>

namespace chan::platform {

void sounds_play(Sound s)
{
    switch (s) {
        case Sound::None: return;
        case Sound::Chime: hal_bridge::app_play_sound(OGG_CHIME); return;
        case Sound::Nudge: hal_bridge::app_play_sound(OGG_NUDGE); return;
        case Sound::Cheer: hal_bridge::app_play_sound(OGG_CHEER); return;
        case Sound::GoalHorn: hal_bridge::app_play_sound(OGG_GOAL_HORN); return;
        case Sound::Aww: hal_bridge::app_play_sound(OGG_AWW); return;
    }
}

}  // namespace chan::platform
