#include "expressions.h"

#include <hal/hal.h>
#include <mooncake_log.h>
#include <stackchan/stackchan.h>

namespace chan::platform {

namespace {

using stackchan::avatar::Emotion;

Expression g_current = Expression::Neutral;
bool g_applied_once = false;
int g_decorator_id = -1;

void drop_decorator(stackchan::avatar::Avatar& avatar)
{
    if (g_decorator_id >= 0) {
        avatar.removeDecorator(g_decorator_id);
        g_decorator_id = -1;
    }
}

}  // namespace

void expressions_apply(Expression e)
{
    auto& sc = GetStackChan();
    if (!sc.hasAvatar()) return;
    if (g_applied_once && e == g_current) return;
    g_current = e;
    g_applied_once = true;

    auto& avatar = sc.avatar();
    drop_decorator(avatar);

    if (e == Expression::Neutral) {
        avatar.setModifyLock(false);
        avatar.setEmotion(Emotion::Neutral);
        return;
    }
    avatar.setModifyLock(true);
    lv_obj_t* parent = lv_screen_active();
    switch (e) {
        case Expression::Attentive:
            avatar.setEmotion(Emotion::Neutral);
            avatar.leftEye().setSize(110);
            avatar.rightEye().setSize(110);
            break;
        case Expression::Thinking:
            avatar.setEmotion(Emotion::Doubt);
            break;
        case Expression::Talking:
            avatar.setEmotion(Emotion::Happy);
            break;
        case Expression::Confused:
            avatar.setEmotion(Emotion::Doubt);
            g_decorator_id = avatar.addDecorator(std::make_unique<stackchan::avatar::SweatDecorator>(parent));
            break;
        case Expression::Worried:
            avatar.setEmotion(Emotion::Sad);
            g_decorator_id = avatar.addDecorator(std::make_unique<stackchan::avatar::SweatDecorator>(parent));
            break;
        case Expression::Happy:
            avatar.setEmotion(Emotion::Happy);
            g_decorator_id = avatar.addDecorator(std::make_unique<stackchan::avatar::HeartDecorator>(parent));
            break;
        case Expression::Sulky:
            avatar.setEmotion(Emotion::Angry);
            break;
        case Expression::Neutral:
            break;
    }
}

void expressions_dance()
{
    auto& sc = GetStackChan();
    sc.addModifier(std::make_unique<stackchan::DanceModifier>(stackchan::DanceModifier::Happy));
}

void expressions_face_user()
{
    auto& motion = GetStackChan().motion();
    motion.yawServo().moveWithSpeed(0, 400);
    motion.pitchServo().moveWithSpeed(-8, 400);
}

}  // namespace chan::platform
