#include "config.h"

#include <ArduinoJson.h>

#include <cctype>
#include <cstdlib>

namespace chan {

namespace {

const char* kRequiredEnv[] = {"WIFI_SSID", "GEMINI_API_KEY", "CHAN_AGENT_TOKEN"};

const std::map<std::string, uint32_t> kDefaultAgentColors = {
    {"claude-code", 0xD97757},
    {"codex", 0x10A37F},
    {"gemini", 0x4285F4},
    {"copilot", 0x8957E5},
};

std::string str_or(JsonVariantConst v, const std::string& fallback)
{
    return v.is<const char*>() ? std::string(v.as<const char*>()) : fallback;
}

int int_or(JsonVariantConst v, int fallback)
{
    return v.is<int>() ? v.as<int>() : fallback;
}

bool bool_or(JsonVariantConst v, bool fallback)
{
    return v.is<bool>() ? v.as<bool>() : fallback;
}

// Integer setting clamped to [lo, hi]; the ranges are documented in docs/config.md.
int int_in(JsonVariantConst v, int fallback, int lo, int hi)
{
    const int value = int_or(v, fallback);
    return value < lo ? lo : value > hi ? hi : value;
}

}  // namespace

int parse_hhmm(std::string_view s)
{
    if (s.size() != 5 || s[2] != ':') return -1;
    for (size_t i : {0u, 1u, 3u, 4u}) {
        if (!std::isdigit(static_cast<unsigned char>(s[i]))) return -1;
    }
    int h = (s[0] - '0') * 10 + (s[1] - '0');
    int m = (s[3] - '0') * 10 + (s[4] - '0');
    if (h > 23 || m > 59) return -1;
    return h * 60 + m;
}

uint32_t parse_hex_color(std::string_view text)
{
    if (!text.empty() && text[0] == '#') text.remove_prefix(1);
    if (text.size() != 6) return 0;
    uint32_t value = 0;
    for (char c : text) {
        value <<= 4;
        if (c >= '0' && c <= '9') value |= c - '0';
        else if (c >= 'a' && c <= 'f') value |= c - 'a' + 10;
        else if (c >= 'A' && c <= 'F') value |= c - 'A' + 10;
        else return 0;
    }
    return value;
}

bool parse_config(std::string_view blob_json, ChanConfig& out, std::string& error)
{
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, blob_json.data(), blob_json.size());
    if (err) {
        error = std::string("config blob is not valid JSON: ") + err.c_str();
        return false;
    }

    ChanConfig cfg;
    JsonObjectConst env = doc["env"].as<JsonObjectConst>();
    for (const char* key : kRequiredEnv) {
        if (!env[key].is<const char*>() || env[key].as<std::string>().empty()) {
            error = std::string("missing required .env key: ") + key;
            return false;
        }
    }
    cfg.wifi_ssid      = env["WIFI_SSID"].as<std::string>();
    cfg.wifi_password  = str_or(env["WIFI_PASSWORD"], "");
    cfg.gemini_api_key = env["GEMINI_API_KEY"].as<std::string>();
    cfg.agent_token    = env["CHAN_AGENT_TOKEN"].as<std::string>();

    JsonObjectConst c = doc["config"].as<JsonObjectConst>();
    cfg.name     = str_or(c["name"], cfg.name);
    cfg.language = str_or(c["language"], cfg.language);
    cfg.timezone = str_or(c["timezone"], cfg.timezone);
    cfg.units    = str_or(c["units"], cfg.units);
    cfg.hostname = str_or(c["hostname"], cfg.hostname);

    cfg.persona_prompt = str_or(c["persona"]["prompt"], "");
    cfg.chattiness     = int_in(c["persona"]["chattiness"], cfg.chattiness, 0, 2);

    cfg.gemini_model  = str_or(c["gemini"]["model"], "");
    cfg.gemini_voice  = str_or(c["gemini"]["voice"], "");
    cfg.gemini_idle_s = int_in(c["gemini"]["sessionIdleSeconds"], cfg.gemini_idle_s, 5, 600);

    cfg.quiet.enabled = bool_or(c["quietHours"]["enabled"], cfg.quiet.enabled);
    if (c["quietHours"]["start"].is<const char*>()) {
        int v = parse_hhmm(c["quietHours"]["start"].as<const char*>());
        if (v >= 0) cfg.quiet.start_min = v;
    }
    if (c["quietHours"]["end"].is<const char*>()) {
        int v = parse_hhmm(c["quietHours"]["end"].as<const char*>());
        if (v >= 0) cfg.quiet.end_min = v;
    }

    cfg.volume     = int_in(c["volume"], cfg.volume, 0, 100);
    cfg.brightness = int_in(c["brightness"], cfg.brightness, 0, 100);

    cfg.screen_agents  = bool_or(c["screens"]["agents"], cfg.screen_agents);
    cfg.screen_sports  = bool_or(c["screens"]["sports"], cfg.screen_sports);
    cfg.screen_weather = bool_or(c["screens"]["weather"], cfg.screen_weather);

    cfg.agents_max_sessions = int_in(c["agents"]["maxSessions"], cfg.agents_max_sessions, 1, 32);
    cfg.agents_stale_min    = int_in(c["agents"]["staleMinutes"], cfg.agents_stale_min, 1, 24 * 60);
    cfg.agents_renudge_min  = int_in(c["agents"]["renudgeMinutes"], cfg.agents_renudge_min, 0, 120);

    cfg.agent_colors = kDefaultAgentColors;
    for (JsonPairConst kv : c["agents"]["colors"].as<JsonObjectConst>()) {
        uint32_t color = parse_hex_color(str_or(kv.value(), ""));
        if (color) cfg.agent_colors[kv.key().c_str()] = color;
    }

    for (JsonObjectConst t : c["teams"].as<JsonArrayConst>()) {
        Team team;
        team.key          = str_or(t["key"], "");
        team.league       = str_or(t["league"], "");
        team.name         = str_or(t["name"], "");
        team.display_name = str_or(t["displayName"], team.name);
        team.color        = parse_hex_color(str_or(t["color"], ""));
        team.alt_color    = parse_hex_color(str_or(t["altColor"], ""));
        if (!team.key.empty() && !team.league.empty()) cfg.teams.push_back(std::move(team));
    }

    out = std::move(cfg);
    error.clear();
    return true;
}

}  // namespace chan
