#include "chan_app.h"

#include <chan/core/quiet_hours.h>
#include <chan/platform/debug_console.h>
#include <chan/platform/expressions.h>
#include <chan/platform/leds.h>
#include <chan/platform/sounds.h>
#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <ctime>

namespace chan::platform {

namespace {
constexpr const char* kTag = "Chan";
constexpr uint32_t kTapDebounceMs  = 2000;
constexpr uint32_t kSwipeSwallowMs = 400;
constexpr uint32_t kAttentionPeriodMs = 50;
constexpr uint32_t kNetPollMs = 500;

int local_minute_of_day()
{
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    return tm_now.tm_hour * 60 + tm_now.tm_min;
}
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

    // Head panel: pat = acknowledge; swipe front-to-back = next screen, back = previous.
    GetHAL().onHeadPetGesture.connect([this](HeadPetGesture g) {
        if (g == HeadPetGesture::Press) {
            acknowledge();
            return;
        }
        if (g != HeadPetGesture::SwipeForward && g != HeadPetGesture::SwipeBackward) return;
        LvglLockGuard lock;
        if (g == HeadPetGesture::SwipeForward) {
            _shell.next();
        } else {
            _shell.prev();
        }
    });

    debug_console_start([this](std::string_view line) { handle_debug_line(line); });
    mclog::tagInfo(kTag, "ui ready: {} screens", _config.screen_agents + _config.screen_sports + 2);
}

void ChanApp::tick()
{
    if (!_ui_ready) return;
    const uint32_t now = GetHAL().millis();
    poll_network(now);
    if (now - _last_attention_ms >= kAttentionPeriodMs) {
        _last_attention_ms = now;
        apply_attention(now);
    }
}

void ChanApp::poll_network(uint32_t now_ms)
{
    if (now_ms - _last_net_poll_ms < kNetPollMs) return;
    _last_net_poll_ms = now_ms;
    const bool online = WifiManager::GetInstance().IsConnected();
    if (online == _net_online) return;
    _net_online = online;
    if (online) {
        _attention.clear(Signal::Offline);
        mclog::tagInfo(kTag, "network back online");
    } else {
        _attention.raise(Signal::Offline, now_ms);
        mclog::tagWarn(kTag, "network offline");
    }
}

bool ChanApp::isQuiet() const
{
    if (_quiet_override == 0) return false;
    if (_quiet_override == 1) return true;
    return in_quiet_hours(_config.quiet, local_minute_of_day());
}

void ChanApp::apply_attention(uint32_t now_ms)
{
    const bool quiet = isQuiet();
    const Output out = _attention.output(now_ms, quiet);
    leds_apply(out, now_ms, -1.0f);
    expressions_apply(out.face);
    const Sound s = _attention.take_sound(quiet);
    if (s != Sound::None) sounds_play(s);
}

void ChanApp::onXiaozhiStatus(const char* status)
{
    if (!status) return;
    const uint32_t now = GetHAL().millis();
    std::string_view st(status);
    // Compare against the English strings used by the stock build (CONFIG_LANGUAGE_EN_US).
    if (st == "Listening...") {
        _attention.clear(Signal::Thinking);
        _attention.clear(Signal::Speaking);
        _attention.raise(Signal::Listening, now);
    } else if (st == "Speaking...") {
        _attention.clear(Signal::Thinking);
        _attention.clear(Signal::Listening);
        _attention.raise(Signal::Speaking, now);
    } else if (st == "Connecting...") {
        _attention.clear(Signal::Listening);
        _attention.clear(Signal::Speaking);
        _attention.raise(Signal::Thinking, now);
    } else if (st == "Standby") {
        _attention.clear(Signal::Listening);
        _attention.clear(Signal::Speaking);
        _attention.clear(Signal::Thinking);
    }
}

void ChanApp::onTap()
{
    const uint32_t now = GetHAL().millis();
    if (now - _shell.last_gesture_ms() < kSwipeSwallowMs) return;
    if (_last_tap_ms != 0 && now - _last_tap_ms < kTapDebounceMs) return;
    if (!hal_bridge::is_xiaozhi_ready()) return;
    if (isQuiet()) {
        mclog::tagInfo(kTag, "tap ignored: quiet hours");
        return;
    }
    _last_tap_ms = now;
    hal_bridge::toggle_xiaozhi_chat_state();
}

void ChanApp::celebrate(uint32_t team_color, uint32_t alt_color, bool horn)
{
    LvglLockGuard lock;
    const uint32_t now = GetHAL().millis();
    _attention.raise(Signal::Celebrate, now, team_color, alt_color);
    if (horn) _attention.queue_sound(Sound::GoalHorn);
    expressions_dance();
}

void ChanApp::sulk()
{
    LvglLockGuard lock;
    _attention.raise(Signal::Sulk, GetHAL().millis());
}

void ChanApp::agentAlert(uint32_t agent_color)
{
    LvglLockGuard lock;
    _attention.raise(Signal::AgentAlert, GetHAL().millis(), agent_color);
    expressions_face_user();
}

void ChanApp::acknowledge()
{
    LvglLockGuard lock;
    _attention.acknowledge();
    mclog::tagInfo(kTag, "acknowledged");
}

void ChanApp::handle_debug_line(std::string_view line)
{
    auto starts = [&](std::string_view p) { return line.substr(0, p.size()) == p; };
    auto arg = [&](std::string_view p) {
        std::string_view a = line.substr(p.size());
        while (!a.empty() && a.front() == ' ') a.remove_prefix(1);
        return a;
    };
    mclog::tagInfo(kTag, "debug: {}", line);
    if (line == "help") {
        mclog::tagInfo(kTag, "commands: attn <offline|listening|thinking|speaking|agent|celebrate|horn|sulk|clear|ack>, quiet <on|off|auto>, screen <face|agents|sports|settings>");
        return;
    }
    if (starts("attn ")) {
        std::string_view a = arg("attn ");
        LvglLockGuard lock;
        const uint32_t now = GetHAL().millis();
        if (a == "offline") _attention.raise(Signal::Offline, now);
        else if (a == "listening") _attention.raise(Signal::Listening, now);
        else if (a == "thinking") _attention.raise(Signal::Thinking, now);
        else if (a == "speaking") _attention.raise(Signal::Speaking, now);
        else if (a == "agent") { _attention.raise(Signal::AgentAlert, now, 0xD97757); expressions_face_user(); }
        else if (a == "celebrate") { _attention.raise(Signal::Celebrate, now, 0xEE7203, 0x1A1A1A); expressions_dance(); }
        else if (a == "horn") { _attention.raise(Signal::Celebrate, now, 0x134A8E, 0xE8291C); _attention.queue_sound(Sound::GoalHorn); expressions_dance(); }
        else if (a == "sulk") _attention.raise(Signal::Sulk, now);
        else if (a == "ack") _attention.acknowledge();
        else if (a == "clear") {
            for (int i = 0; i < static_cast<int>(Signal::Count_); ++i) _attention.clear(static_cast<Signal>(i));
        } else mclog::tagWarn(kTag, "unknown signal");
        return;
    }
    if (starts("quiet ")) {
        std::string_view a = arg("quiet ");
        _quiet_override = a == "on" ? 1 : a == "off" ? 0 : -1;
        mclog::tagInfo(kTag, "quiet override = {} (now quiet: {})", _quiet_override, isQuiet());
        return;
    }
    if (starts("screen ")) {
        std::string_view a = arg("screen ");
        LvglLockGuard lock;
        if (a == "face") _shell.show(Screen::Face);
        else if (a == "agents") _shell.show(Screen::Agents);
        else if (a == "sports") _shell.show(Screen::Sports);
        else if (a == "settings") _shell.show(Screen::Settings);
        return;
    }
    mclog::tagWarn(kTag, "unknown command; try `help`");
}

}  // namespace chan::platform
