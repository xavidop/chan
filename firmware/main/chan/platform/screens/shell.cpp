#include "shell.h"

#include <chan/platform/chan_app.h>
#include <hal/hal.h>
#include <mooncake_log.h>

namespace chan::platform {

namespace {
constexpr const char* kTag = "Shell";
constexpr uint32_t kBg = 0x101418;
constexpr uint32_t kFg = 0xF2F5F7;
constexpr uint32_t kMuted = 0x8A94A6;
}  // namespace

void Shell::init(lv_obj_t* parent, const ChanConfig& cfg)
{
    _order.clear();
    _order.push_back(Screen::Face);
    if (cfg.screen_agents) _order.push_back(Screen::Agents);
    if (cfg.screen_sports) _order.push_back(Screen::Sports);
    if (cfg.screen_weather) _order.push_back(Screen::Weather);

    _agents = make_overlay(parent, "Agents", "Nothing to show yet");
    _sports = make_overlay(parent, "Sports", "No games right now");
    _weather = make_overlay(parent, "Weather", "Fetching the forecast…");

    lv_obj_add_event_cb(parent, gesture_cb, LV_EVENT_GESTURE, this);
    lv_obj_add_event_cb(parent, press_cb, LV_EVENT_PRESSING, this);
    lv_obj_add_event_cb(parent, press_cb, LV_EVENT_PRESSED, this);
    lv_obj_add_event_cb(parent, press_cb, LV_EVENT_RELEASED, this);
    _current = Screen::Face;
}

lv_obj_t* Shell::make_overlay(lv_obj_t* parent, const char* title, const char* hint)
{
    lv_obj_t* root = lv_obj_create(parent);
    lv_obj_set_size(root, 320, 240);
    lv_obj_align(root, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_color(root, lv_color_hex(kBg), 0);
    lv_obj_set_style_bg_opa(root, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root, 0, 0);
    lv_obj_set_style_radius(root, 0, 0);
    lv_obj_set_style_pad_all(root, 0, 0);
    lv_obj_remove_flag(root, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(root, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_CLICKABLE | LV_OBJ_FLAG_GESTURE_BUBBLE | LV_OBJ_FLAG_EVENT_BUBBLE | LV_OBJ_FLAG_HIDDEN));
    lv_obj_add_event_cb(root, click_cb, LV_EVENT_CLICKED, this);

    lv_obj_t* label = lv_label_create(root);
    lv_label_set_text(label, title);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(kFg), 0);
    lv_obj_align(label, LV_ALIGN_TOP_LEFT, 16, 14);

    lv_obj_t* sub = lv_label_create(root);
    lv_label_set_text(sub, hint);
    lv_obj_set_style_text_font(sub, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(sub, lv_color_hex(kMuted), 0);
    lv_obj_align(sub, LV_ALIGN_CENTER, 0, 0);
    return root;
}

void Shell::set_visible(lv_obj_t* obj, bool visible)
{
    if (!obj) return;
    if (visible) {
        lv_obj_remove_flag(obj, LV_OBJ_FLAG_HIDDEN);
        lv_obj_move_foreground(obj);
    } else {
        lv_obj_add_flag(obj, LV_OBJ_FLAG_HIDDEN);
    }
}

void Shell::show(Screen screen)
{
    if (screen == Screen::Settings) {
        mclog::tagInfo(kTag, "settings requested");
        _settings_requested = true;
        return;
    }
    _current = screen;
    set_visible(_agents, screen == Screen::Agents);
    set_visible(_sports, screen == Screen::Sports);
    set_visible(_weather, screen == Screen::Weather);
    mclog::tagInfo(kTag, "screen -> {}", static_cast<int>(screen));
}

void Shell::next()
{
    for (size_t i = 0; i < _order.size(); ++i) {
        if (_order[i] == _current) {
            show(_order[(i + 1) % _order.size()]);
            return;
        }
    }
    show(Screen::Face);
}

void Shell::prev()
{
    for (size_t i = 0; i < _order.size(); ++i) {
        if (_order[i] == _current) {
            show(_order[(i + _order.size() - 1) % _order.size()]);
            return;
        }
    }
    show(Screen::Face);
}

void Shell::gesture_cb(lv_event_t* e)
{
    auto* self = static_cast<Shell*>(lv_event_get_user_data(e));
    const lv_dir_t dir = lv_indev_get_gesture_dir(lv_indev_active());
    if (dir != LV_DIR_LEFT && dir != LV_DIR_RIGHT) return;
    self->_last_gesture_ms = GetHAL().millis();
    if (dir == LV_DIR_LEFT) {
        self->next();
    } else {
        self->prev();
    }
}

void Shell::press_cb(lv_event_t* e)
{
    auto* self = static_cast<Shell*>(lv_event_get_user_data(e));
    const lv_event_code_t code = lv_event_get_code(e);
    const uint32_t now = GetHAL().millis();
    if (code == LV_EVENT_PRESSED) {
        self->_press_started_ms = now;
        self->_long_press_fired = false;
    } else if (code == LV_EVENT_PRESSING) {
        if (!self->_long_press_fired && self->_press_started_ms && now - self->_press_started_ms >= 2000) {
            self->_long_press_fired = true;
            self->_last_long_press_ms = now;
            if (ChanApp::get().otaInProgress()) {
                // A warm reboot now would leave a half-written OTA partition; try again after the update.
                mclog::tagWarn(kTag, "settings refused: firmware update in progress");
            } else {
                self->show(Screen::Settings);
            }
        }
    } else if (code == LV_EVENT_RELEASED) {
        self->_press_started_ms = 0;
    }
}

void Shell::click_cb(lv_event_t* e)
{
    auto* self = static_cast<Shell*>(lv_event_get_user_data(e));
    if (self->on_tap) self->on_tap();
}

}  // namespace chan::platform
