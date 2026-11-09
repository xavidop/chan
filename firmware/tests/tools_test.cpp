#include <doctest.h>

#include <chan/core/tools.h>

#include <ArduinoJson.h>

#include <stdexcept>

using namespace chan;

namespace {
FunctionDecl decl(const char* name)
{
    return FunctionDecl{name, std::string("desc ") + name, R"({"type":"OBJECT","properties":{}})"};
}
}  // namespace

TEST_CASE("declarations lists added tools")
{
    ToolRegistry r;
    r.add(decl("look"), [](const std::string&) { return std::string("{}"); });
    r.add(decl("dance"), [](const std::string&) { return std::string("{}"); });
    auto d = r.declarations();
    REQUIRE(d.size() == 2);
    CHECK(d[0].name == "look");
    CHECK(d[1].name == "dance");
    CHECK(d[1].description == "desc dance");
}

TEST_CASE("add replaces an existing tool with the same name")
{
    ToolRegistry r;
    r.add(decl("get_weather"), [](const std::string&) { return std::string(R"({"v":1})"); });
    r.add(decl("get_weather"), [](const std::string&) { return std::string(R"({"v":2})"); });
    CHECK(r.declarations().size() == 1);
    CHECK(r.dispatch("get_weather", "{}") == R"({"v":2})");
}

TEST_CASE("dispatch calls tool with args")
{
    ToolRegistry r;
    std::string seen;
    r.add(decl("set_head_angles"), [&](const std::string& args) {
        seen = args;
        return std::string(R"({"ok":true})");
    });
    CHECK(r.dispatch("set_head_angles", R"({"yaw":10,"pitch":-5})") == R"({"ok":true})");
    CHECK(seen == R"({"yaw":10,"pitch":-5})");
}

TEST_CASE("unknown tool returns error json")
{
    ToolRegistry r;
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, r.dispatch("nope", "{}")) == DeserializationError::Ok);
    CHECK((doc["error"] == "unknown tool nope"));
}

TEST_CASE("tool exception becomes error json")
{
    ToolRegistry r;
    r.add(decl("boom"), [](const std::string&) -> std::string { throw std::runtime_error("camera busy"); });
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, r.dispatch("boom", "{}")) == DeserializationError::Ok);
    CHECK((doc["error"] == "camera busy"));
}

TEST_CASE("tool returning invalid json becomes error json")
{
    ToolRegistry r;
    r.add(decl("bad"), [](const std::string&) { return std::string("not json"); });
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, r.dispatch("bad", "{}")) == DeserializationError::Ok);
    CHECK(doc["error"].is<const char*>());
}

TEST_CASE("arg helpers read numbers strings and defaults")
{
    CHECK(arg_int(R"({"yaw":12})", "yaw", 0) == 12);
    CHECK(arg_int(R"({"yaw":12.7})", "yaw", 0) == 12);
    CHECK(arg_int(R"({})", "yaw", 7) == 7);
    CHECK(arg_int("garbage", "yaw", 3) == 3);
    CHECK(arg_string(R"({"screen":"agents"})", "screen", "face") == "agents");
    CHECK(arg_string(R"({})", "screen", "face") == "face");
}
