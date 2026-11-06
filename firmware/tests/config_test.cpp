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
