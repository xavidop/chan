// Chan screen shell: Face (the avatar, always underneath), Agents and Sports overlays,
// and Settings (the stock setup app, reached through a warm reboot).
// All methods must be called with the LVGL lock held.
#pragma once

#include <chan/core/config.h>

#include <lvgl.h>

#include <functional>
#include <vector>

namespace chan::platform {

enum class Screen { Face, Agents, Sports, Weather, Settings };

class Shell {
public:
    // Creates the (hidden) overlays as children of `parent` (the active screen that
    // holds the avatar) and registers swipe/tap handlers on it.
    void init(lv_obj_t* parent, const ChanConfig& cfg);

    void show(Screen screen);
    void next();
    void prev();
    Screen current() const { return _current; }

    // Tap anywhere on an overlay (the Face's own tap is handled by the avatar panel).
    std::function<void()> on_tap;

    // Milliseconds (HAL clock) of the last horizontal swipe; used to swallow the
    // click LVGL may also report for the same touch.
    uint32_t last_gesture_ms() const { return _last_gesture_ms; }
    // Holding the screen for 2 s opens Settings; the release click that follows must be ignored.
    uint32_t last_long_press_ms() const { return _last_long_press_ms; }

    // show(Settings) only records the request: NVS writes are not allowed from the LVGL
    // task (its stack lives in PSRAM). ChanApp::tick() consumes it and reboots.
    bool takeSettingsRequest() { bool r = _settings_requested; _settings_requested = false; return r; }

    // Overlay roots for later tasks to draw into (nullptr before init()).
    lv_obj_t* agents_root() const { return _agents; }
    lv_obj_t* sports_root() const { return _sports; }
    lv_obj_t* weather_root() const { return _weather; }

private:
    lv_obj_t* make_overlay(lv_obj_t* parent, const char* title, const char* hint);
    void set_visible(lv_obj_t* obj, bool visible);
    static void gesture_cb(lv_event_t* e);
    static void click_cb(lv_event_t* e);
    static void press_cb(lv_event_t* e);
    uint32_t _press_started_ms = 0;
    bool _long_press_fired = false;
    uint32_t _last_long_press_ms = 0;

    std::vector<Screen> _order;  // Face first; Agents/Sports/Weather when enabled (Settings: long press)
    Screen _current = Screen::Face;
    lv_obj_t* _agents = nullptr;
    lv_obj_t* _sports = nullptr;
    lv_obj_t* _weather = nullptr;
    uint32_t _last_gesture_ms = 0;
    bool _settings_requested = false;
};

}  // namespace chan::platform
