#include "chan_app.h"

#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <ssid_manager.h>

namespace chan::platform {

namespace {
constexpr const char* kTag = "Chan";
constexpr uint32_t kTapDebounceMs    = 2000;
constexpr uint32_t kSwipeSwallowMs   = 400;
}  // namespace

ChanApp& ChanApp::get()
{
    static ChanApp instance;
    return instance;
}

void ChanApp::init(const ChanConfig& cfg)
{
    _config     = cfg;
    _has_config = true;
    provision_wifi();
}

void ChanApp::provision_wifi()
{
    auto& ssids      = SsidManager::GetInstance();
    const auto& list = ssids.GetSsidList();
    const bool same = list.size() == 1 && list[0].ssid == _config.wifi_ssid && list[0].password == _config.wifi_password;
    if (same) return;
    ssids.Clear();
    ssids.AddSsid(_config.wifi_ssid, _config.wifi_password);
    mclog::tagInfo(kTag, "wifi credentials updated from config (ssid: {})", _config.wifi_ssid);
}

void ChanApp::onXiaozhiReady()
{
    if (_ui_ready) return;
    _ui_ready = true;
    _shell.init(lv_screen_active(), _config);
    _shell.on_tap = [this]() { onTap(); };

    // Head panel: swipe front-to-back = next screen, back-to-front = previous.
    GetHAL().onHeadPetGesture.connect([this](HeadPetGesture g) {
        if (g != HeadPetGesture::SwipeForward && g != HeadPetGesture::SwipeBackward) return;
        LvglLockGuard lock;
        if (g == HeadPetGesture::SwipeForward) {
            _shell.next();
        } else {
            _shell.prev();
        }
    });
    mclog::tagInfo(kTag, "ui ready: {} screens", _config.screen_agents + _config.screen_sports + 2);
}

void ChanApp::tick()
{
    // Later tasks: attention, pollers. Nothing yet.
}

void ChanApp::onTap()
{
    const uint32_t now = GetHAL().millis();
    if (now - _shell.last_gesture_ms() < kSwipeSwallowMs) return;
    if (_last_tap_ms != 0 && now - _last_tap_ms < kTapDebounceMs) return;
    if (!hal_bridge::is_xiaozhi_ready()) return;
    _last_tap_ms = now;
    hal_bridge::toggle_xiaozhi_chat_state();
}

}  // namespace chan::platform
