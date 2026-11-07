#include <doctest.h>

#include <chan/core/config.h>

#include "test_support.h"

using namespace chan;

TEST_CASE("parses full example")
{
    ChanConfig cfg;
    std::string err;
    REQUIRE_MESSAGE(parse_config(chan_test::example_blob(), cfg, err), err);
    CHECK(cfg.name == "Chan");
    CHECK(cfg.language == "es");
    CHECK(cfg.quiet.enabled);
    CHECK(cfg.quiet.start_min == 1380);
    CHECK(cfg.quiet.end_min == 420);
    CHECK(cfg.teams.size() == 6);
    CHECK(cfg.teams[0].key == "valencia");
    CHECK(cfg.teams[0].color == 0xEE7203);
    CHECK(cfg.agent_colors.at("codex") == 0x10A37F);
    CHECK(cfg.agent_colors.at("claude-code") == 0xD97757);
    CHECK(cfg.wifi_ssid == "your-wifi-name");
    CHECK(cfg.gemini_model.rfind("gemini", 0) == 0);
    CHECK(cfg.hostname == "chan");
    CHECK(cfg.screen_sports);
}

TEST_CASE("defaults when optional keys missing")
{
    ChanConfig cfg;
    std::string err;
    const char* blob = R"({"env":{"WIFI_SSID":"s","WIFI_PASSWORD":"p","GEMINI_API_KEY":"k","CHAN_AGENT_TOKEN":"t"},"config":{}})";
    REQUIRE_MESSAGE(parse_config(blob, cfg, err), err);
    CHECK(cfg.name == "Chan");
    CHECK(cfg.volume == 70);
    CHECK(cfg.brightness == 70);
    CHECK(cfg.gemini_idle_s == 20);
    CHECK(cfg.quiet.enabled);
    CHECK(cfg.quiet.start_min == 23 * 60);
    CHECK(cfg.quiet.end_min == 7 * 60);
    CHECK(cfg.agents_max_sessions == 8);
    CHECK(cfg.agents_stale_min == 120);
    CHECK(cfg.agents_renudge_min == 5);
    CHECK(cfg.hostname == "chan");
    CHECK(cfg.units == "metric");
    CHECK(cfg.timezone == "auto");
    CHECK(cfg.chattiness == 1);
    CHECK(cfg.teams.empty());
    CHECK(cfg.agent_colors.at("gemini") == 0x4285F4);
    CHECK(cfg.agent_colors.at("copilot") == 0x8957E5);
    CHECK(cfg.screen_agents);
    CHECK(cfg.screen_weather);
}

TEST_CASE("missing GEMINI key is reported")
{
    ChanConfig cfg;
    std::string err;
    const char* blob = R"({"env":{"WIFI_SSID":"s","CHAN_AGENT_TOKEN":"t"},"config":{}})";
    CHECK_FALSE(parse_config(blob, cfg, err));
    CHECK(err.find("GEMINI_API_KEY") != std::string::npos);
}

TEST_CASE("missing WIFI_SSID is reported")
{
    ChanConfig cfg;
    std::string err;
    const char* blob = R"({"env":{"GEMINI_API_KEY":"k","CHAN_AGENT_TOKEN":"t"},"config":{}})";
    CHECK_FALSE(parse_config(blob, cfg, err));
    CHECK(err.find("WIFI_SSID") != std::string::npos);
}

TEST_CASE("invalid json returns error")
{
    ChanConfig cfg;
    std::string err;
    CHECK_FALSE(parse_config("{not json", cfg, err));
    CHECK_FALSE(err.empty());
}

TEST_CASE("parse_hhmm rejects 24:00 and 7:5")
{
    CHECK(parse_hhmm("23:00") == 1380);
    CHECK(parse_hhmm("07:00") == 420);
    CHECK(parse_hhmm("00:00") == 0);
    CHECK(parse_hhmm("24:00") == -1);
    CHECK(parse_hhmm("7:5") == -1);
    CHECK(parse_hhmm("") == -1);
    CHECK(parse_hhmm("ab:cd") == -1);
}

TEST_CASE("parse_hex_color accepts # prefix and bare hex")
{
    CHECK(parse_hex_color("#EE7203") == 0xEE7203);
    CHECK(parse_hex_color("ee7203") == 0xEE7203);
    CHECK(parse_hex_color("nope") == 0);
}

TEST_CASE("numeric settings are clamped to their documented ranges")
{
    ChanConfig cfg;
    std::string err;
    const char* blob = R"({"env":{"WIFI_SSID":"s","GEMINI_API_KEY":"k","CHAN_AGENT_TOKEN":"t"},
        "config":{"volume":250,"brightness":-5,"persona":{"chattiness":9},
                  "gemini":{"sessionIdleSeconds":0},
                  "agents":{"maxSessions":0,"staleMinutes":100000,"renudgeMinutes":-1}}})";
    REQUIRE_MESSAGE(parse_config(blob, cfg, err), err);
    CHECK(cfg.volume == 100);
    CHECK(cfg.brightness == 0);
    CHECK(cfg.chattiness == 2);
    CHECK(cfg.gemini_idle_s == 5);
    CHECK(cfg.agents_max_sessions == 1);
    CHECK(cfg.agents_stale_min == 1440);
    CHECK(cfg.agents_renudge_min == 0);

    const char* high = R"({"env":{"WIFI_SSID":"s","GEMINI_API_KEY":"k","CHAN_AGENT_TOKEN":"t"},
        "config":{"gemini":{"sessionIdleSeconds":9999},"agents":{"maxSessions":500,"renudgeMinutes":999}}})";
    REQUIRE_MESSAGE(parse_config(high, cfg, err), err);
    CHECK(cfg.gemini_idle_s == 600);
    CHECK(cfg.agents_max_sessions == 32);
    CHECK(cfg.agents_renudge_min == 120);
}

TEST_CASE("in-range numeric settings are kept as given")
{
    ChanConfig cfg;
    std::string err;
    const char* blob = R"({"env":{"WIFI_SSID":"s","GEMINI_API_KEY":"k","CHAN_AGENT_TOKEN":"t"},
        "config":{"volume":0,"brightness":100,"gemini":{"sessionIdleSeconds":45},
                  "agents":{"maxSessions":3,"staleMinutes":1,"renudgeMinutes":0}}})";
    REQUIRE_MESSAGE(parse_config(blob, cfg, err), err);
    CHECK(cfg.volume == 0);
    CHECK(cfg.brightness == 100);
    CHECK(cfg.gemini_idle_s == 45);
    CHECK(cfg.agents_max_sessions == 3);
    CHECK(cfg.agents_stale_min == 1);
    CHECK(cfg.agents_renudge_min == 0);
}
