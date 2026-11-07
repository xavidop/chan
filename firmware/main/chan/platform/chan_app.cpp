#include "chan_app.h"

#include <chan/core/quiet_hours.h>
#include <chan/platform/camera_tool.h>
#include <chan/platform/debug_console.h>
#include <chan/platform/gemini_protocol.h>
#include <chan/platform/http_fetch.h>
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

#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <freertos/idf_additions.h>
#include <esp_heap_caps.h>
#include <esp_ota_ops.h>

#include <algorithm>
#include <set>
#include <ctime>

namespace chan::platform {

namespace {
constexpr const char* kTag = "Chan";
constexpr uint32_t kTapDebounceMs  = 2000;
constexpr uint32_t kSwipeSwallowMs = 400;
constexpr uint32_t kAttentionPeriodMs = 50;
constexpr uint32_t kNetPollMs = 500;

// kUnknownMinuteOfDay until SNTP has set the clock (and the timezone has been applied), so
// quiet hours cannot kick in on the 1970 UTC the device boots with.
int local_minute_of_day()
{
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (!clock_is_valid(tm_now.tm_year + 1900)) return kUnknownMinuteOfDay;
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
    if (_config.timezone != "auto" && !_config.timezone.empty()) {
        apply_timezone(_config.timezone);
        _tz_applied = true;
    }
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
        _agent_alert_from_store = true;
        if (!isQuiet()) expressions_face_user();
    } else if (!_agents->any_unacknowledged_attention(now)) {
        _attention.clear(Signal::AgentAlert);
        _agent_alert_from_store = false;
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
    if (_shell.sports_root()) _sports_screen.init(_shell.sports_root());
    if (_shell.weather_root()) _weather_screen.init(_shell.weather_root());
    _face_overlay.init(lv_screen_active());
    _next_sports_poll_ms = GetHAL().millis() + 15000;  // let the audio engine finish allocating first
    // Stack in PSRAM: this task only does network I/O and parsing (never NVS/flash writes).
    // Lowest priority: big feed downloads must never starve the UI, audio or I2C tasks.
    xTaskCreatePinnedToCoreWithCaps(net_task, "chan_net", 16384, this, 1, nullptr, 0, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);

    // Head panel: pat = acknowledge; swipe front-to-back = next screen, back = previous.
    // The capacitive head panel produces phantom presses and swipes, so: swipes are not used for
    // navigation (the screen swipe is), and a pat only counts as a press held 400 ms–3 s then released.
    GetHAL().onHeadPetGesture.connect([this](HeadPetGesture g) {
        const uint32_t now = GetHAL().millis();
        mclog::tagInfo(kTag, "head gesture {}", static_cast<int>(g));
        if (g == HeadPetGesture::Press) {
            _head_press_ms = now;
            return;
        }
        if (g == HeadPetGesture::Release) {
            const uint32_t held = _head_press_ms ? now - _head_press_ms : 0;
            _head_press_ms = 0;
            if (held >= 400 && held <= 3000) acknowledge();
            return;
        }
        // SwipeForward / SwipeBackward: ignored (too noisy on this sensor).
    });

    // Config is the source of truth for volume and brightness; Settings sliders tweak live.
    GetHAL().setSpeakerVolume(static_cast<uint8_t>(_config.volume), false);
    GetHAL().setBackLightBrightness(static_cast<uint8_t>(_config.brightness), false);

    debug_console_start([this](std::string_view line) { handle_debug_line(line); });
    mclog::tagInfo(kTag, "ui ready: {} screens", _config.screen_agents + _config.screen_sports + _config.screen_weather + 2);
}

void ChanApp::tick()
{
    if (!_ui_ready) return;
    confirm_running_image_if_stable();
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
        if (!isQuiet()) expressions_face_user();
    }
    // An alert whose rows all went stale (agents.staleMinutes) has nothing actionable behind it:
    // the Agents screen hides those rows, so the LEDs must stop asking for attention too.
    if (_agent_alert_from_store && _agents && !_agents->any_unacknowledged_attention(now)) {
        _agent_alert_from_store = false;
        if (_attention.is_active(Signal::AgentAlert, now)) {
            _attention.clear(Signal::AgentAlert);
            _agents_dirty = true;
            mclog::tagInfo(kTag, "agent alert dropped: rows went stale");
        }
    }
    refresh_agents_screen(now, false);
    refresh_sports_screen(now, false);
    refresh_weather_screen(now, false);
    if (_banner_until_ms != 0 && static_cast<int32_t>(now - _banner_until_ms) >= 0) {
        _banner_until_ms = 0;
        _sports_screen.hide_banner();
        if (_shell.current() == Screen::Sports) _shell.show(_banner_return);
    }
    if (now - _last_clock_ms >= 1000) {
        _last_clock_ms = now;
        std::string tz;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            tz.swap(_pending_tz);
        }
        if (!tz.empty()) apply_timezone(tz);
        _face_overlay.set_time(localTimeHHMM());
        bool dirty = false;
        std::optional<Weather> w;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            dirty = _weather_dirty;
            _weather_dirty = false;
            w = _weather;
        }
        if (dirty) _face_overlay.set_weather(w, _config.units);
    }
    _face_overlay.set_visible(_config.screen_weather && _shell.current() == Screen::Face && _idle_face);
    if (now - _last_attention_ms >= kAttentionPeriodMs) {
        _last_attention_ms = now;
        apply_attention(now);
    }
}

void ChanApp::poll_network(uint32_t now_ms)
{
    if (now_ms - _last_net_poll_ms < kNetPollMs) return;
    _last_net_poll_ms = now_ms;
    const bool wifi = WifiManager::GetInstance().IsConnected();
    if (wifi != _net_online) {
        _net_online = wifi;
        mclog::tagInfo(kTag, "wi-fi {}", wifi ? "connected" : "lost");
    }
    // Being associated is not being online: a router without uplink keeps the station happy
    // while every fetch fails. Both count as offline for the LEDs and the face.
    const bool feeds_down = _fetch_health.down();
    const bool offline = !wifi || feeds_down;
    if (offline == _offline_shown) return;
    _offline_shown = offline;
    if (offline) {
        _attention.raise(Signal::Offline, now_ms);
        mclog::tagWarn(kTag, "network offline ({})", !wifi ? "no wi-fi" : "feeds keep failing");
    } else {
        _attention.clear(Signal::Offline);
        mclog::tagInfo(kTag, "network back online");
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
    _idle_face = out.face == Expression::Neutral;
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
        if (_agents && _agents->any_unacknowledged_attention(now)) {
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
    if (now - _shell.last_long_press_ms() < 1500) return;
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
    if (!isQuiet()) expressions_dance();
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
    if (!isQuiet()) expressions_face_user();
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
    _tools.add({"show_screen", "Switch what the screen shows. screen: face, agents (coding agents status), sports (live scores and last results) or weather.",
                R"({"type":"OBJECT","properties":{"screen":{"type":"STRING"}}})"},
               [this](const std::string& args) -> std::string {
                   const std::string which = arg_string(args, "screen", "face");
                   LvglLockGuard lock;
                   if (which == "agents") _shell.show(Screen::Agents);
                   else if (which == "sports") _shell.show(Screen::Sports);
                   else if (which == "weather") _shell.show(Screen::Weather);
                   else _shell.show(Screen::Face);
                   return R"({"ok":true})";
               });
    // Data tools: filled in by the agents, sports and weather features; empty until then.
    _tools.add({"get_agent_status", "Status of the owner's coding agents (Claude Code, Codex, Gemini CLI): which are working, done, or need attention.", kNoParams},
               [this](const std::string&) -> std::string { return agentStatusJson(); });
    _tools.add({"get_scores", "Live/upcoming games today for the owner's followed teams, plus each team's last result.", kNoParams},
               [this](const std::string&) -> std::string { return scoresJson(); });
    _tools.add({"get_weather", "Current local weather, today's high/low and the local time at the owner's location.", kNoParams},
               [this](const std::string&) -> std::string { return weatherJson(); });
}

void ChanApp::confirm_running_image_if_stable()
{
    // With rollback enabled, a freshly updated image is only PENDING_VERIFY until something
    // confirms it — otherwise the next reboot silently goes back to the previous firmware.
    // Being up for 30 s with the UI running, Wi-Fi connected and the config loaded is the proof.
    if (_image_confirmed || !_has_config || !_net_online || GetHAL().millis() < 30000) return;
    _image_confirmed = true;
    const esp_partition_t* running = esp_ota_get_running_partition();
    esp_ota_img_states_t state;
    if (running && esp_ota_get_state_partition(running, &state) == ESP_OK && state == ESP_OTA_IMG_PENDING_VERIFY) {
        esp_ota_mark_app_valid_cancel_rollback();
        mclog::tagInfo(kTag, "firmware image confirmed (rollback cancelled)");
    }
}

void ChanApp::apply_timezone(const std::string& iana_or_posix)
{
    const std::string posix = looks_like_iana(iana_or_posix) ? posix_tz_for(iana_or_posix) : iana_or_posix;
    GetHAL().setTimezone(posix);
    mclog::tagInfo(kTag, "timezone {} -> {}", iana_or_posix, posix);
}

std::string ChanApp::localTimeHHMM() const
{
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year + 1900 < 2020) return "--:--";
    char buf[8];
    snprintf(buf, sizeof(buf), "%02d:%02d", tm_now.tm_hour, tm_now.tm_min);
    return buf;
}

std::string ChanApp::weatherJson()
{
    std::optional<Location> loc;
    std::optional<Weather> w;
    {
        std::lock_guard<std::mutex> lock(_wx_mutex);
        loc = _loc;
        w = _weather;
    }
    if (!loc || !w) return R"({"available":false,"reason":"weather not fetched yet"})";
    return weather_json(*loc, *w, _config.units, localTimeHHMM());
}

void ChanApp::net_task(void* arg)
{
    static_cast<ChanApp*>(arg)->net_loop();
    vTaskDelete(nullptr);
}

void ChanApp::net_loop()
{
    constexpr uint32_t kWeatherPeriodMs = 15 * 60 * 1000;
    constexpr uint32_t kRetryMs = 60 * 1000;
    uint32_t next_attempt_ms = 0;
    while (true) {
        vTaskDelay(pdMS_TO_TICKS(2000));
        if (!_net_online || _ota_in_progress) continue;
        const uint32_t now = GetHAL().millis();
        bool have_location = false;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            have_location = _loc.has_value();
        }
        // Location first (it is what sets the clock's timezone and takes one tiny request); sports
        // never wait on the weather itself.
        if (have_location || GetHAL().millis() > 60000) {
            poll_sports(now);
            sports_lookback();
        }
        if (static_cast<int32_t>(now - next_attempt_ms) < 0) continue;

        std::optional<Location> loc;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            loc = _loc;
        }
        if (!loc) {
            JsonDocument doc(psram_allocator());
            std::string err;
            if (!http_get_json(ipwho_url(), doc, nullptr, err, 16 * 1024)) {
                _fetch_health.record(false);
                mclog::tagWarn(kTag, "location lookup failed: {}", err);
                next_attempt_ms = now + kRetryMs;
                continue;
            }
            _fetch_health.record(true);
            loc = parse_ipwho(doc.as<JsonVariantConst>());
            if (!loc) {
                mclog::tagWarn(kTag, "location response not usable");
                next_attempt_ms = now + kRetryMs;
                continue;
            }
            mclog::tagInfo(kTag, "location: {} ({})", loc->city, loc->timezone);
            {
                std::lock_guard<std::mutex> lock(_wx_mutex);
                _loc = loc;
            }
            if (!_tz_applied && !loc->timezone.empty()) {
                std::lock_guard<std::mutex> lock(_wx_mutex);
                _pending_tz = loc->timezone;  // applied by tick() on an internal-stack task (NVS write)
                _tz_applied = true;
            }
        }

        bool need_weather = false;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            need_weather = !_weather || now - _weather_fetched_ms >= kWeatherPeriodMs;
        }
        if (!need_weather) continue;
        JsonDocument doc(psram_allocator());
        std::string err;
        if (!http_get_json(open_meteo_url(loc->lat, loc->lon, _config.units), doc, nullptr, err, 32 * 1024)) {
            _fetch_health.record(false);
            mclog::tagWarn(kTag, "weather fetch failed: {}", err);
            next_attempt_ms = now + kRetryMs;
            continue;
        }
        _fetch_health.record(true);
        auto w = parse_open_meteo(doc.as<JsonVariantConst>());
        if (!w) {
            mclog::tagWarn(kTag, "weather response not usable");
            next_attempt_ms = now + kRetryMs;
            continue;
        }
        mclog::tagInfo(kTag, "weather: {:.1f} code {} ({}) high {:.1f} low {:.1f}", w->temp, w->code,
                       condition_name(icon_for_code(w->code)), w->high, w->low);
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            _weather = w;
            _weather_fetched_ms = now;
            _weather_dirty = true;
        }
    }
}

namespace {
std::string local_date(const char* fmt, int days_back = 0)
{
    time_t now = time(nullptr) - static_cast<time_t>(days_back) * 86400;
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year + 1900 < 2020) return "";
    char buf[16];
    strftime(buf, sizeof(buf), fmt, &tm_now);
    return buf;
}

std::vector<Game> fetch_league(const std::string& league, int days_back, std::string& err)
{
    std::vector<Game> games;
    JsonDocument doc(psram_allocator());
    if (league == "mlb") {
        const std::string d = local_date("%Y-%m-%d", days_back);
        if (d.empty() || !http_get_json(mlb_schedule_url(d), doc, &mlb_filter(), err)) return games;
        return parse_mlb_schedule(doc.as<JsonVariantConst>());
    }
    const std::string d = days_back > 0 ? local_date("%Y%m%d", days_back) : "";
    if (days_back > 0 && d.empty()) return games;
    if (!http_get_json(espn_scoreboard_url(league, d), doc, &espn_filter(), err)) return games;
    return parse_espn_scoreboard(doc.as<JsonVariantConst>(), league);
}
}  // namespace

void ChanApp::poll_sports(uint32_t now_ms)
{
    if (_config.teams.empty() || !_config.screen_sports) return;
    if (static_cast<int32_t>(now_ms - _next_sports_poll_ms) < 0) return;
    std::set<std::string> leagues;
    for (const auto& t : _config.teams) leagues.insert(t.league);
    std::vector<Game> all;
    std::set<std::string> fetched;
    bool any_error = false;
    for (const auto& league : leagues) {
        std::string err;
        auto games = fetch_league(league, 0, err);
        if (!err.empty()) {
            any_error = true;
            mclog::tagWarn(kTag, "sports {}: {}", league, err);
            continue;
        }
        fetched.insert(league);
        all.insert(all.end(), games.begin(), games.end());
        vTaskDelay(pdMS_TO_TICKS(300));
    }
    // One league failing while another answers is the feed's problem, not an outage.
    if (!fetched.empty()) _fetch_health.record(true);
    else if (any_error) _fetch_health.record(false);
    std::vector<ScoreEvent> events;
    bool live = false, pending = false;
    if (!fetched.empty()) {
        std::lock_guard<std::mutex> lock(_wx_mutex);
        events = _scores.update(all, _config.teams, fetched);
        _followed_games = _scores.followed_games();
        _last_results = _scores.last_results();
        _sports_dirty = true;
        for (const auto& g : _followed_games) {
            if (g.status == GameStatus::Live) live = true;
            else if (g.status == GameStatus::Scheduled) pending = true;
        }
    }
    const uint32_t interval = live ? 15000u : pending ? 600000u : 3600000u;
    _next_sports_poll_ms = now_ms + (any_error && all.empty() ? 60000u : interval);
    mclog::tagInfo(kTag, "sports: {} games, {} followed ({}), {} tracked, next poll in {} s", all.size(), _followed_games.size(),
                   live ? "live" : pending ? "upcoming" : "quiet", _scores.tracked_count(), (_next_sports_poll_ms - now_ms) / 1000);
    for (const auto& e : events) onScoreEvent(e);
}

namespace {
std::vector<Team> teams_missing_result(const std::string& league, const std::vector<Team>& teams, const std::map<std::string, Game>& last)
{
    std::vector<Team> out;
    for (const auto& t : teams) {
        if (t.league == league && !last.count(t.key)) out.push_back(t);
    }
    return out;
}
}  // namespace

void ChanApp::sports_lookback()
{
    // Once a day: recent scoreboards first, then each still-missing team's season schedule, so the
    // Sports screen can always show a "last result" (off-season teams included).
    if (_config.teams.empty() || !_config.screen_sports) return;
    time_t now = time(nullptr);
    struct tm tm_now;
    localtime_r(&now, &tm_now);
    if (tm_now.tm_year + 1900 < 2020 || tm_now.tm_yday == _lookback_yday) return;
    _lookback_yday = tm_now.tm_yday;
    const int season = tm_now.tm_year + 1900;
    std::set<std::string> leagues;
    for (const auto& t : _config.teams) leagues.insert(t.league);
    for (const auto& league : leagues) {
        for (int day = 1; day <= 3; ++day) {
            std::map<std::string, Game> last;
            {
                std::lock_guard<std::mutex> lock(_wx_mutex);
                last = _scores.last_results();
            }
            if (teams_missing_result(league, _config.teams, last).empty()) break;
            std::string err;
            auto games = fetch_league(league, day, err);
            if (!err.empty()) {
                mclog::tagWarn(kTag, "lookback {} -{}d: {}", league, day, err);
                break;
            }
            std::lock_guard<std::mutex> lock(_wx_mutex);
            _scores.remember_results(games, _config.teams);
            _last_results = _scores.last_results();
            _sports_dirty = true;
        }
        std::map<std::string, Game> last;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            last = _scores.last_results();
        }
        auto missing = teams_missing_result(league, _config.teams, last);
        if (missing.empty()) continue;
        // Resolve team ids from the league's team list, then read each team's season schedule.
        std::vector<TeamRef> refs;
        {
            JsonDocument doc(psram_allocator());
            std::string err;
            const bool ok = league == "mlb" ? http_get_json(mlb_teams_url(), doc, &mlb_teams_filter(), err)
                                            : http_get_json(espn_teams_url(league), doc, &espn_teams_filter(), err);
            if (!ok) {
                mclog::tagWarn(kTag, "team list {}: {}", league, err);
                continue;
            }
            refs = league == "mlb" ? parse_mlb_teams(doc.as<JsonVariantConst>()) : parse_espn_teams(doc.as<JsonVariantConst>());
        }
        for (const auto& team : missing) {
            const std::string id = find_team_id(refs, team);
            if (id.empty()) {
                mclog::tagWarn(kTag, "no team id for {} in {}", team.name, league);
                continue;
            }
            for (int attempt = 0; attempt < 2; ++attempt) {  // this season, then last season (early in the year)
                JsonDocument doc(psram_allocator());
                std::string err;
                std::vector<Game> games;
                if (league == "mlb") {
                    if (!http_get_json(mlb_team_schedule_url(id, season - attempt), doc, &mlb_filter(), err)) break;
                    games = parse_mlb_schedule(doc.as<JsonVariantConst>());
                } else {
                    if (!http_get_json(espn_team_schedule_url(league, id), doc, &espn_filter(), err)) break;
                    games = parse_espn_scoreboard(doc.as<JsonVariantConst>(), league);
                }
                bool found = false;
                {
                    std::lock_guard<std::mutex> lock(_wx_mutex);
                    _scores.remember_results(games, _config.teams);
                    _last_results = _scores.last_results();
                    found = _last_results.count(team.key) > 0;
                    _sports_dirty = true;
                }
                mclog::tagInfo(kTag, "schedule {} ({}): {} games, last result {}", team.name, id, games.size(), found ? "found" : "none");
                if (found || league != "mlb") break;
            }
            vTaskDelay(pdMS_TO_TICKS(300));
        }
    }
    mclog::tagInfo(kTag, "sports lookback done: {} teams with a last result", _last_results.size());
}

void ChanApp::onScoreEvent(const ScoreEvent& e)
{
    const Team* team = nullptr;
    for (const auto& t : _config.teams) {
        if (t.key == e.team_key) team = &t;
    }
    if (!team) return;
    const bool horn = e.sport == Sport::Soccer || e.sport == Sport::Hockey;
    mclog::tagInfo(kTag, "score event: {} {} {} (+{})", e.ours ? "OURS" : "opponent", team->display_name, e.line, e.points);
    LvglLockGuard lock;
    const uint32_t now = GetHAL().millis();
    if (_shell.current() != Screen::Sports) _banner_return = _shell.current();
    _shell.show(Screen::Sports);
    _sports_dirty = true;
    if (e.ours) {
        _attention.raise(Signal::Celebrate, now, team->color, team->alt_color);
        if (horn) _attention.queue_sound(Sound::GoalHorn);
        if (!isQuiet()) expressions_dance();
        _sports_screen.show_banner(e.line, team->color, team->alt_color);
    } else {
        _attention.raise(Signal::Sulk, now);
        _sports_screen.hide_banner();
    }
    _banner_until_ms = now + Attention::kDefaultTimedMs;
}

std::string ChanApp::scoresJson()
{
    std::lock_guard<std::mutex> lock(_wx_mutex);
    return games_json(_followed_games, _last_results);
}

void ChanApp::debugFakeScore(const std::string& league, const std::string& team_key, bool opponent)
{
    const Team* team = nullptr;
    for (const auto& t : _config.teams) {
        if (t.key == team_key && t.league == league) team = &t;
    }
    if (!team) {
        mclog::tagWarn(kTag, "fake-score: no followed team {} in {}", team_key, league);
        return;
    }
    Game g;
    g.id = "fake-" + team_key;
    g.league = league;
    g.home = Side{team->name, "", 0};
    g.away = Side{"Rivals FC", "RIV", 0};
    g.status = GameStatus::Live;
    g.clock = "test";
    g.start_epoch = static_cast<uint64_t>(time(nullptr));
    const int pts = sport_for_league(league) == Sport::Football ? 6 : sport_for_league(league) == Sport::Basketball ? 3 : 1;
    std::vector<ScoreEvent> events;
    {
        std::lock_guard<std::mutex> lock(_wx_mutex);
        _scores.update({g}, _config.teams);
        if (opponent) g.away.score += pts;
        else g.home.score += pts;
        events = _scores.update({g}, _config.teams);
        _followed_games = _scores.followed_games();
        _sports_dirty = true;
    }
    for (const auto& e : events) onScoreEvent(e);
}

void ChanApp::refresh_sports_screen(uint32_t now_ms, bool force)
{
    if (!_shell.sports_root() || _shell.current() != Screen::Sports) return;
    static Screen last_screen = Screen::Face;
    if (last_screen != _shell.current()) {
        last_screen = _shell.current();
        force = true;
    }
    if (!force && !_sports_dirty && now_ms - _sports_rendered_ms < 15000) return;
    std::vector<Game> games;
    std::map<std::string, Game> last;
    {
        std::lock_guard<std::mutex> lock(_wx_mutex);
        games = _followed_games;
        last = _last_results;
        _sports_dirty = false;
    }
    _sports_screen.render(games, last, _config.teams, static_cast<uint64_t>(time(nullptr)));
    _sports_rendered_ms = now_ms;
}

void ChanApp::refresh_weather_screen(uint32_t now_ms, bool force)
{
    if (!_shell.weather_root() || _shell.current() != Screen::Weather) return;
    if (!force && now_ms - _weather_rendered_ms < 5000) return;
    std::optional<Location> loc;
    std::optional<Weather> w;
    {
        std::lock_guard<std::mutex> lock(_wx_mutex);
        loc = _loc;
        w = _weather;
    }
    _weather_screen.render(loc, w, _config.units, localTimeHHMM());
    _weather_rendered_ms = now_ms;
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
        mclog::tagInfo(kTag, "commands: attn <offline|listening|thinking|speaking|agent|celebrate|horn|sulk|clear|ack>, quiet <on|off|auto>, screen <face|agents|sports|weather|settings>, talk, look, weather, overlay, agent <state>, fake-score <league> <team_key> [opp]");
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
        else if (a == "agent") { _attention.raise(Signal::AgentAlert, now, 0xD97757); if (!isQuiet()) expressions_face_user(); }
        else if (a == "celebrate") { _attention.raise(Signal::Celebrate, now, 0xEE7203, 0x1A1A1A); if (!isQuiet()) expressions_dance(); }
        else if (a == "horn") { _attention.raise(Signal::Celebrate, now, 0x134A8E, 0xE8291C); _attention.queue_sound(Sound::GoalHorn); if (!isQuiet()) expressions_dance(); }
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
    if (starts("fake-score ")) {  // fake-score <league> <team_key> [opp]
        std::string a(arg("fake-score "));
        auto sp1 = a.find(' ');
        if (sp1 == std::string::npos) { mclog::tagWarn(kTag, "usage: fake-score <league> <team_key> [opp]"); return; }
        std::string league = a.substr(0, sp1);
        std::string rest = a.substr(sp1 + 1);
        auto sp2 = rest.find(' ');
        std::string key = rest.substr(0, sp2);
        bool opp = sp2 != std::string::npos && rest.substr(sp2 + 1) == "opp";
        debugFakeScore(league, key, opp);
        return;
    }
    if (line == "overlay") {
        bool w = false;
        {
            std::lock_guard<std::mutex> lock(_wx_mutex);
            w = _weather.has_value();
        }
        mclog::tagInfo(kTag, "overlay: screen={} idle_face={} quiet={} weather={} screen_weather={} time={}", static_cast<int>(_shell.current()),
                       _idle_face, isQuiet(), w, _config.screen_weather, localTimeHHMM());
        return;
    }
    if (line == "weather") {
        mclog::tagInfo(kTag, "{} @ {}", weatherJson(), localTimeHHMM());
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
        else if (a == "weather") _shell.show(Screen::Weather);
        else if (a == "settings") _shell.show(Screen::Settings);
        return;
    }
    mclog::tagWarn(kTag, "unknown command; try `help`");
}

}  // namespace chan::platform
