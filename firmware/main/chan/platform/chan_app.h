// ChanApp owns everything Chan adds on top of the stock avatar/voice runtime:
// the parsed config, the screen shell, and (in later tasks) attention, agents,
// sports and weather. It is a singleton driven from the stackchan update task.
#pragma once

#include <chan/core/config.h>
#include <chan/platform/screens/shell.h>

namespace chan::platform {

// Warm-reboot target that boots into the stock Settings app instead of Chan.
constexpr int kSettingsWarmBootTarget = 1;

class ChanApp {
public:
    static ChanApp& get();

    // Called once at boot after HAL init: keeps the config and pushes the Wi-Fi
    // credentials into the station's SSID store when they differ.
    void init(const ChanConfig& cfg);
    bool hasConfig() const { return _has_config; }
    const ChanConfig& config() const { return _config; }

    // Called (under the LVGL lock) once the avatar exists and xiaozhi is ready.
    void onXiaozhiReady();
    // Called (under the LVGL lock) every ~20 ms from the stackchan update task.
    void tick();

    // Tap anywhere on the face or an overlay: start/stop listening, with the stock
    // 2 s debounce and swallowing the click that follows a swipe.
    void onTap();

    Shell& shell() { return _shell; }

private:
    void provision_wifi();

    ChanConfig _config;
    bool _has_config = false;
    bool _ui_ready   = false;
    Shell _shell;
    uint32_t _last_tap_ms = 0;
};

}  // namespace chan::platform
