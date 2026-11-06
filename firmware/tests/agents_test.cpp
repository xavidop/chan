#include <doctest.h>

#include <chan/core/agents.h>

#include <ArduinoJson.h>

using namespace chan;

namespace {
AgentEvent ev(const char* agent, const char* session, const char* state, const char* project = "chan")
{
    return AgentEvent{agent, session, project, state, ""};
}
}  // namespace

TEST_CASE("parses valid event")
{
    AgentEvent e;
    std::string err;
    REQUIRE(parse_agent_event(R"({"agent":"claude-code","session":"abc","project":"chan","state":"needs_attention","message":"Allow Bash?"})", e, err));
    CHECK(e.agent == "claude-code");
    CHECK(e.session == "abc");
    CHECK(e.project == "chan");
    CHECK(e.state == "needs_attention");
    CHECK(e.message == "Allow Bash?");
}

TEST_CASE("rejects unknown state")
{
    AgentEvent e;
    std::string err;
    CHECK_FALSE(parse_agent_event(R"({"agent":"codex","session":"s","state":"sleeping"})", e, err));
    CHECK(err.find("state") != std::string::npos);
}

TEST_CASE("rejects missing agent")
{
    AgentEvent e;
    std::string err;
    CHECK_FALSE(parse_agent_event(R"({"session":"s","state":"done"})", e, err));
    CHECK(err.find("agent") != std::string::npos);
    CHECK_FALSE(parse_agent_event(R"({"agent":"codex","state":"done"})", e, err));
    CHECK(err.find("session") != std::string::npos);
}

TEST_CASE("rejects body over 4096 bytes")
{
    std::string big = R"({"agent":"codex","session":"s","state":"done","message":")" + std::string(5000, 'x') + "\"}";
    AgentEvent e;
    std::string err;
    CHECK_FALSE(parse_agent_event(big, e, err));
    CHECK(err.find("large") != std::string::npos);
    CHECK_FALSE(parse_agent_event("{bad", e, err));
}

TEST_CASE("message is truncated to 120 chars")
{
    std::string json = R"({"agent":"codex","session":"s","state":"done","message":")" + std::string(300, 'm') + "\"}";
    AgentEvent e;
    std::string err;
    REQUIRE(parse_agent_event(json, e, err));
    CHECK(e.message.size() == 120);
}

TEST_CASE("needs_attention reports change once")
{
    AgentStore store(8, 120 * 60000, 5 * 60000);
    CHECK(store.apply(ev("claude-code", "a", "working"), 1000) == Change::Other);
    CHECK(store.apply(ev("claude-code", "a", "needs_attention"), 2000) == Change::BecameNeedsAttention);
    CHECK(store.apply(ev("claude-code", "a", "needs_attention"), 3000) == Change::None);
    CHECK(store.any_unacknowledged_attention());
    CHECK(store.apply(ev("claude-code", "a", "done"), 4000) == Change::Other);
    CHECK_FALSE(store.any_unacknowledged_attention());
}

TEST_CASE("ended removes row")
{
    AgentStore store(8, 120 * 60000, 5 * 60000);
    store.apply(ev("codex", "s1", "working"), 0);
    CHECK(store.rows(10).size() == 1);
    store.apply(ev("codex", "s1", "ended"), 20);
    CHECK(store.rows(30).empty());
}

TEST_CASE("stale rows hidden after stale_ms")
{
    AgentStore store(8, 1000, 5000);
    store.apply(ev("codex", "s1", "done"), 0);
    CHECK(store.rows(999).size() == 1);
    CHECK(store.rows(1000).empty());
}

TEST_CASE("store evicts oldest beyond max sessions")
{
    AgentStore store(2, 60000, 5000);
    store.apply(ev("codex", "s1", "working"), 0);
    store.apply(ev("codex", "s2", "working"), 10);
    store.apply(ev("codex", "s3", "working"), 20);
    auto rows = store.rows(30);
    REQUIRE(rows.size() == 2);
    for (const auto& r : rows) CHECK(r.session != "s1");
}

TEST_CASE("renudge fires once after delay")
{
    AgentStore store(8, 60000, 5000);
    store.apply(ev("gemini", "g", "needs_attention"), 0);
    CHECK_FALSE(store.take_renudge(4999));
    CHECK(store.take_renudge(5000));
    CHECK_FALSE(store.take_renudge(20000));
}

TEST_CASE("acknowledge stops renudge")
{
    AgentStore store(8, 60000, 5000);
    store.apply(ev("gemini", "g", "needs_attention"), 0);
    store.acknowledge_all();
    CHECK_FALSE(store.any_unacknowledged_attention());
    CHECK_FALSE(store.take_renudge(6000));
    auto rows = store.rows(6000);
    REQUIRE(rows.size() == 1);
    CHECK(rows[0].acknowledged);
    CHECK(rows[0].state == AgentState::NeedsAttention);
}

TEST_CASE("attention rows sort first then most recent")
{
    AgentStore store(8, 60000, 5000);
    store.apply(ev("codex", "old-working", "working"), 0);
    store.apply(ev("claude-code", "needs", "needs_attention"), 10);
    store.apply(ev("gemini", "new-working", "working"), 20);
    auto rows = store.rows(30);
    REQUIRE(rows.size() == 3);
    CHECK(rows[0].session == "needs");
    CHECK(rows[1].session == "new-working");
    CHECK(rows[2].session == "old-working");
    CHECK(rows[0].since_ms == 10);
}

TEST_CASE("to_json lists rows with state names")
{
    AgentStore store(8, 60000, 5000);
    store.apply(ev("claude-code", "abc", "needs_attention", "dash"), 1000);
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, store.to_json(31000)) == DeserializationError::Ok);
    REQUIRE(doc["agents"].size() == 1);
    CHECK((doc["agents"][0]["agent"] == "claude-code"));
    CHECK((doc["agents"][0]["project"] == "dash"));
    CHECK((doc["agents"][0]["state"] == "needs_attention"));
    CHECK((doc["agents"][0]["seconds_ago"] == 30));
}
