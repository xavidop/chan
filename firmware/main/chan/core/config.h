// Chan configuration model and parser. Pure C++17: no ESP-IDF includes, host-tested.
//
// The device receives one JSON blob: {"env": {KEY: value…}, "config": {…}} —
// `env` is the flattened .env file (secrets, Wi-Fi) and `config` is config.json.
// scripts/chan.py pack-config produces it; platform/config_store reads it from flash.
#pragma once

#include <cstdint>
#include <map>
#include <string>
#include <string_view>
#include <vector>

namespace chan {

struct QuietHours {
    bool enabled = true;
    int start_min = 23 * 60;  // minutes after midnight
    int end_min   = 7 * 60;
};

struct Team {
    std::string key, league, name, display_name;
    uint32_t color = 0, alt_color = 0;  // 0xRRGGBB
};

struct ChanConfig {
    // from env
    std::string wifi_ssid, wifi_password, gemini_api_key, agent_token;
    // from config
    std::string name = "Chan";
    std::string language = "es";
    std::string timezone = "auto";
    std::string units = "metric";
    std::string hostname = "chan";
    std::string persona_prompt;
    std::string gemini_model;
    std::string gemini_voice;
    // Integers are clamped by parse_config to the ranges in docs/config.md.
    int chattiness = 1;         // 0..2
    int gemini_idle_s = 20;     // 5..600
    int volume = 70;            // 0..100
    int brightness = 70;        // 0..100
    int agents_max_sessions = 8;  // 1..32
    int agents_stale_min = 120;   // 1..1440
    int agents_renudge_min = 5;   // 0..120, 0 = no re-nudge
    QuietHours quiet;
    bool screen_agents = true, screen_sports = true, screen_weather = true;
    std::map<std::string, uint32_t> agent_colors;
    std::vector<Team> teams;
};

// Parses the blob into `out`. Missing optional keys keep the defaults above.
// Returns false (with a human-readable `error`) on invalid JSON or when a
// required env key (WIFI_SSID, GEMINI_API_KEY, CHAN_AGENT_TOKEN) is missing/empty.
bool parse_config(std::string_view blob_json, ChanConfig& out, std::string& error);

// "HH:MM" (two-digit fields, 00:00–23:59) → minutes after midnight, else -1.
int parse_hhmm(std::string_view hhmm);

// "#RRGGBB" or "RRGGBB" → 0xRRGGBB; 0 when malformed.
uint32_t parse_hex_color(std::string_view text);

}  // namespace chan
