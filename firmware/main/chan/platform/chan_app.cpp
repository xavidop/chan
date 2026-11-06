#include "chan_app.h"

#include <chan/core/quiet_hours.h>
#include <chan/platform/camera_tool.h>
#include <chan/platform/debug_console.h>
#include <chan/platform/gemini_protocol.h>
#include <chan/platform/http_server.h>
#include <stackchan/stackchan.h>
#include <chan/platform/expressions.h>
#include <chan/platform/leds.h>
#include <chan/platform/sounds.h>
#include <hal/board/hal_bridge.h>
#include <hal/hal.h>
#include <mooncake_log.h>
#include <ssid_manager.h>
#include <wifi_manager.h>

#include <algorithm>
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
    _agents = std::make_unique<AgentStore>(cfg.agents_max_sessions, static_cast<uint32_t>(cfg.agents_stale_min) * 60000u,
                                           static_cast<uint32_t>(cfg.agents_renudge_min) * 60000u);
    register_tools();
}

void ChanApp::onAgentEvent(const AgentEvent& event)
{
    LvglLockGuard lock;
    if (!_agents) return;
    const uint32_t now = GetHAL().millis();
    const Change change = _agents->apply(event, now);
    mclog::tagInfo(kTag, "agent {} [{}] {} -> {}", event.agent, event.project, event.state, static_cast<int>(change));
    if (change == Change::BecameNeedsAttention) {
        auto it = _config.agent_colors.find(event.agent);
        const uint32_t color = it != _config.agent_colors.end() ? it->second : Attention::kDefaultAgentColor;
        _attention.raise(Signal::AgentAlert, now, color);
        expressions_face_user();
    } else if (!_agents->any_unacknowledged_attention()) {
        _attention.clear(Signal::AgentAlert);
    }
    _agents_dirty = true;
}

std::string ChanApp::agentStatusJson()
{
    LvglLockGuard lock;
    if (!_agents) return R"({"agents":[]})";
    return _agents->to_json(GetHAL().millis());
}

void ChanApp::refresh_agents_screen(uint32_t now_ms, bool force)
{
    if (!_agents || !_shell.agents_root()) return;
    if (_shell.current() != Screen::Agents) return;
    static Screen last_screen = Screen::Face;
    if (last_screen != _shell.current()) {
        last_screen = _shell.current();
        force = true;
    }
    if (!force && !_agents_dirty && now_ms - _agents_rendered_ms < 10000) return;
    _agents_screen.render(_agents->rows(now_ms), now_ms, _config.agent_colors);
    _agents_dirty = false;
    _agents_rendered_ms = now_ms;
}

void ChanApp::setMood(Expression e, uint32_t hold_ms)
{
    _mood = e;
    _mood_until_ms = GetHAL().millis() + hold_ms;
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
    if (_shell.agents_root()) _agents_screen.init(_shell.agents_root());

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

    // Config is the source of truth for volume and brightness; Settings sliders tweak live.
    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_config.volume), false);
    GetHAL().setBackLightBrightness(static_cast<uint8_t>(_config.brightness), false);

    debug_console_start([this](std::string_view line) { handle_debug_line(line); });
    mclog::tagInfo(kTag, "ui ready: {} screens", _config.screen_agents + _config.screen_sports + 2);
}

void ChanApp::tick()
{
    if (!_ui_ready) return;
    if (_shell.takeSettingsRequest()) {
        mclog::tagInfo(kTag, "opening settings");
        GetHAL().requestWarmReboot(kSettingsWarmBootTarget);  // never returns
    }
    const uint32_t now = GetHAL().millis();
    poll_network(now);
    if (_net_online && !_http_started) {
        _http_started = true;
        http_server_start(_config);
    }
    if (_agents && _agents->take_renudge(now)) {
        mclog::tagInfo(kTag, "agent re-nudge");
        _attention.queue_sound(Sound::Nudge);
        expressions_face_user();
    }
    refresh_agents_screen(now, false);
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
    Output out = _attention.output(now_ms, quiet);
    if (_mood_until_ms != 0) {
        if (static_cast<int32_t>(now_ms - _mood_until_ms) >= 0) {
            _mood_until_ms = 0;
        } else if (out.face == Expression::Neutral || out.face == Expression::Attentive || out.face == Expression::Talking) {
            out.face = _mood;
        }
    }
    const float level = out.led == LedPattern::VoiceLevel ? _voice_level : -1.0f;
    leds_apply(out, now_ms, level);
    if (_voice_level > 0) _voice_level *= 0.85f;  // decay between chunks
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
        if (_agents && _agents->any_unacknowledged_attention()) {
            _agents->acknowledge_all();
            _attention.acknowledge();
            _agents_dirty = true;
        }
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
    if (_agents) {
        _agents->acknowledge_all();
        _agents_dirty = true;
    }
    mclog::tagInfo(kTag, "acknowledged");
}

GeminiSetup ChanApp::buildGeminiSetup() const
{
    GeminiSetup s;
    s.model = _config.gemini_model.empty() ? "gemini-3.8-live" : _config.gemini_model;
    s.voice = _config.gemini_voice;
    s.language = _config.language;
    s.system_prompt = build_system_prompt(_config);
    s.google_search = true;
    s.functions = _tools.declarations();
    return s;
}

std::string ChanApp::handleToolCall(const ServerMsg::Call& call)
{
    std::string result = _tools.dispatch(call.name, call.args_json);
    mclog::tagInfo(kTag, "tool {} -> {}", call.name, result.substr(0, 120));
    return result;
}

namespace {
const char* kNoParams = R"({"type":"OBJECT","properties":{}})";

Expression expression_from_name(const std::string& n)
{
    if (n == "happy") return Expression::Happy;
    if (n == "worried" || n == "sad") return Expression::Worried;
    if (n == "thinking" || n == "curious") return Expression::Thinking;
    if (n == "sulky" || n == "angry") return Expression::Sulky;
    if (n == "confused") return Expression::Confused;
    if (n == "attentive") return Expression::Attentive;
    return Expression::Neutral;
}
}  // namespace

void ChanApp::register_tools()
{
    _tools.add({"take_photo", "Take a photo with your camera and attach it to the conversation so you can SEE what is in front of you. Use it when the user asks what you see, what they are holding or showing you, or to read something. This does not move your head.", kNoParams},
               [this](const std::string&) -> std::string {
                   std::vector<uint8_t> jpeg;
                   std::string err;
                   if (!camera_capture_jpeg(jpeg, err, 90)) return error_json(err);
                   if (!_gemini || !_gemini->SendImage(jpeg.data(), jpeg.size())) return error_json("could not send the image");
                   return R"({"ok":true,"note":"the photo is attached as an image in this conversation"})";
               });
    _tools.add({"turn_head", "Physically turn your head (look left/right/up/down, face someone, nod). yaw: -90 (your left) to 90 (your right), 0 = straight ahead. pitch: -20 (down) to 60 (up), 0 = level. Keep within ±45 for natural looks. This does not take a photo.",
                R"({"type":"OBJECT","properties":{"yaw":{"type":"INTEGER"},"pitch":{"type":"INTEGER"}}})"},
               [](const std::string& args) -> std::string {
                   const int yaw = arg_int(args, "yaw", -9999);
                   const int pitch = arg_int(args, "pitch", -9999);
                   LvglLockGuard lock;
                   auto& motion = GetStackChan().motion();
                   if (yaw != -9999) motion.yawServo().moveWithSpeed(std::max(-90, std::min(90, yaw)) * 10, 200);
                   if (pitch != -9999) motion.pitchServo().moveWithSpeed(std::max(-20, std::min(60, pitch)) * 10, 200);
                   return R"({"ok":true})";
               });
    _tools.add({"get_head_angles", "Where your head is pointing right now (yaw, pitch in degrees).", kNoParams},
               [](const std::string&) -> std::string {
                   LvglLockGuard lock;
                   auto& motion = GetStackChan().motion();
                   return fmt::format(R"({{"yaw":{},"pitch":{}}})", motion.yawServo().getCurrentAngle() / 10,
                                      motion.pitchServo().getCurrentAngle() / 10);
               });
    _tools.add({"set_expression", "Show an emotion on your face for a few seconds. expression: happy, worried, thinking, sulky, confused, attentive or neutral.",
                R"({"type":"OBJECT","properties":{"expression":{"type":"STRING"},"seconds":{"type":"INTEGER"}}})"},
               [this](const std::string& args) -> std::string {
                   const int seconds = std::max(1, std::min(60, arg_int(args, "seconds", 8)));
                   setMood(expression_from_name(arg_string(args, "expression", "neutral")), static_cast<uint32_t>(seconds) * 1000);
                   return R"({"ok":true})";
               });
    _tools.add({"dance", "Do a short happy dance (a few seconds). Use when the user asks you to dance or celebrate.", kNoParams},
               [this](const std::string&) -> std::string {
                   LvglLockGuard lock;
                   expressions_dance();
                   setMood(Expression::Happy, 6000);
                   return R"({"ok":true})";
               });
    _tools.add({"show_screen", "Switch what the screen shows. screen: face, agents (coding agents status) or sports (live scores).",
                R"({"type":"OBJECT","properties":{"screen":{"type":"STRING"}}})"},
               [this](const std::string& args) -> std::string {
                   const std::string which = arg_string(args, "screen", "face");
                   LvglLockGuard lock;
                   if (which == "agents") _shell.show(Screen::Agents);
                   else if (which == "sports") _shell.show(Screen::Sports);
                   else _shell.show(Screen::Face);
                   return R"({"ok":true})";
               });
    // Data tools: filled in by the agents, sports and weather features; empty until then.
    _tools.add({"get_agent_status", "Status of the owner's coding agents (Claude Code, Codex, Gemini CLI): which are working, done, or need attention.", kNoParams},
               [this](const std::string&) -> std::string { return agentStatusJson(); });
    _tools.add({"get_scores", "Live and upcoming games for the owner's followed teams.", kNoParams},
               [](const std::string&) -> std::string { return R"({"games":[]})"; });
    _tools.add({"get_weather", "Current local weather and time at the owner's location.", kNoParams},
               [](const std::string&) -> std::string { return R"({"available":false})"; });
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
        mclog::tagInfo(kTag, "commands: attn <offline|listening|thinking|speaking|agent|celebrate|horn|sulk|clear|ack>, quiet <on|off|auto>, screen <face|agents|sports|settings>, talk, look, agent <state>");
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
    if (starts("agent ")) {  // agent <needs_attention|working|done|ended> [name]
        std::string_view a = arg("agent ");
        AgentEvent e;
        e.agent = "claude-code";
        e.session = "debug";
        e.project = "chan";
        e.state = std::string(a.substr(0, a.find(' ')));
        e.message = "debug event";
        onAgentEvent(e);
        return;
    }
    if (line == "look") {
        std::vector<uint8_t> jpeg;
        std::string err;
        if (camera_capture_jpeg(jpeg, err)) mclog::tagInfo(kTag, "look: {} bytes jpeg", jpeg.size());
        else mclog::tagWarn(kTag, "look failed: {}", err);
        return;
    }
    if (line == "talk") {
        hal_bridge::toggle_xiaozhi_chat_state();
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
