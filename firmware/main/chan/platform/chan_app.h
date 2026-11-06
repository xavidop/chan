// ChanApp owns everything Chan adds on top of the stock avatar/voice runtime:
// the parsed config, the screen shell, attention (LEDs/face/sounds) and, in later
// tasks, agents, sports and weather. It is a singleton driven from the stackchan
// update task (every ~20 ms, under the LVGL lock).
#pragma once

#include <chan/core/agents.h>
#include <chan/core/attention.h>
#include <chan/core/config.h>
#include <chan/core/gemini_messages.h>
#include <chan/core/tools.h>
#include <chan/core/weather.h>
#include <chan/platform/screens/agents_screen.h>
#include <chan/platform/screens/face_overlay.h>
#include <chan/platform/screens/shell.h>

#include <memory>
#include <mutex>
#include <optional>

#include <string_view>

namespace chan::platform {

class GeminiProtocol;

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
    // Called by the display whenever xiaozhi changes its status string
    // (Listening… / Speaking… / Standby / Connecting…).
    void onXiaozhiStatus(const char* status);

    // Tap anywhere on the face or an overlay: start/stop listening, with the stock
    // 2 s debounce and swallowing the click that follows a swipe.
    void onTap();

    // High-level events (take the LVGL lock themselves; safe from any task).
    void celebrate(uint32_t team_color, uint32_t alt_color, bool horn);
    void sulk();
    void agentAlert(uint32_t agent_color);
    void acknowledge();

    bool isQuiet() const;

    // Session description for Gemini Live (model, voice, language, persona, tools).
    GeminiSetup buildGeminiSetup() const;
    // Tool dispatch for Gemini function calls; returns a JSON object string.
    std::string handleToolCall(const ServerMsg::Call& call);
    // Loudness of the reply being played (0..1), fed by the protocol, consumed by the LEDs.
    void setOutputLevel(float level) { _voice_level = level; }
    // The live protocol instance (owned by the xiaozhi Application); used by the `look` tool.
    void setGeminiProtocol(GeminiProtocol* p) { _gemini = p; }
    ToolRegistry& tools() { return _tools; }
    // A mood the model asked for (set_expression): shown while no stronger signal is active.
    void setMood(Expression e, uint32_t hold_ms);

    // Weather/location (filled by the network task; safe from any task).
    std::string weatherJson();
    std::string localTimeHHMM() const;

    // Agent status (from the HTTP API; safe from any task).
    void onAgentEvent(const AgentEvent& event);
    std::string agentStatusJson();

    Shell& shell() { return _shell; }
    Attention& attention() { return _attention; }

private:
    void provision_wifi();
    void handle_debug_line(std::string_view line);
    void poll_network(uint32_t now_ms);
    void apply_attention(uint32_t now_ms);

    ChanConfig _config;
    bool _has_config = false;
    bool _ui_ready   = false;
    Shell _shell;
    Attention _attention;
    uint32_t _last_tap_ms = 0;
    uint32_t _last_attention_ms = 0;
    uint32_t _last_net_poll_ms = 0;
    bool _net_online = true;
    int _quiet_override = -1;  // -1 auto, 0 force off, 1 force on (debug)
    float _voice_level = -1.0f;
    ToolRegistry _tools;
    GeminiProtocol* _gemini = nullptr;
    Expression _mood = Expression::Neutral;
    uint32_t _mood_until_ms = 0;
    void register_tools();
    void refresh_agents_screen(uint32_t now_ms, bool force);
    std::unique_ptr<AgentStore> _agents;
    AgentsScreen _agents_screen;
    bool _agents_dirty = true;
    uint32_t _agents_rendered_ms = 0;
    bool _http_started = false;
    static void net_task(void* arg);
    void net_loop();
    void apply_timezone(const std::string& iana_or_posix);
    FaceOverlay _face_overlay;
    mutable std::mutex _wx_mutex;
    std::optional<Location> _loc;
    std::optional<Weather> _weather;
    uint32_t _weather_fetched_ms = 0;
    bool _weather_dirty = false;
    bool _tz_applied = false;
    bool _idle_face = true;
    uint32_t _last_clock_ms = 0;
};

}  // namespace chan::platform
