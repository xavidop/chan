#include <doctest.h>

#include <chan/core/base64.h>
#include <chan/core/gemini_messages.h>

#include "test_support.h"

using namespace chan;

TEST_CASE("b64 roundtrip")
{
    for (size_t n : {0u, 1u, 2u, 3u, 4u, 5u, 17u, 1920u}) {
        std::vector<uint8_t> in(n);
        for (size_t i = 0; i < n; ++i) in[i] = static_cast<uint8_t>(i * 7 + 3);
        std::string enc = b64_encode(in.data(), in.size());
        CHECK(enc.size() == ((n + 2) / 3) * 4);
        std::vector<uint8_t> out;
        REQUIRE(b64_decode(enc, out));
        CHECK(out == in);
    }
    CHECK(b64_encode(reinterpret_cast<const uint8_t*>("Man"), 3) == "TWFu");
    CHECK(b64_encode(reinterpret_cast<const uint8_t*>("Ma"), 2) == "TWE=");
    std::vector<uint8_t> bad;
    CHECK_FALSE(b64_decode("TW!u", bad));
}

TEST_CASE("setup contains model voice and googleSearch")
{
    GeminiSetup s;
    s.model = "gemini-3.8-live";
    s.voice = "Aoede";
    s.language = "es";
    s.system_prompt = "You are Chan.";
    s.google_search = true;
    s.functions.push_back({"look", "Take a photo.", R"({"type":"OBJECT","properties":{}})"});
    s.functions.push_back({"dance", "Dance.", R"({"type":"OBJECT","properties":{"seconds":{"type":"INTEGER"}}})"});

    std::string json = build_setup(s);
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, json) == DeserializationError::Ok);
    CHECK((doc["setup"]["model"] == "models/gemini-3.8-live"));
    CHECK((doc["setup"]["generationConfig"]["responseModalities"][0] == "AUDIO"));
    CHECK((doc["setup"]["generationConfig"]["speechConfig"]["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] == "Aoede"));
    CHECK((doc["setup"]["generationConfig"]["speechConfig"]["languageCode"] == "es-ES"));
    CHECK((doc["setup"]["systemInstruction"]["parts"][0]["text"] == "You are Chan."));
    CHECK(doc["setup"]["tools"][0]["googleSearch"].is<JsonObjectConst>());
    CHECK((doc["setup"]["tools"][1]["functionDeclarations"].size() == 2));
    CHECK((doc["setup"]["tools"][1]["functionDeclarations"][1]["parameters"]["properties"]["seconds"]["type"] == "INTEGER"));
    CHECK(doc["setup"]["inputAudioTranscription"].is<JsonObjectConst>());
    CHECK(doc["setup"]["outputAudioTranscription"].is<JsonObjectConst>());
}

TEST_CASE("setup without functions has only googleSearch tool")
{
    GeminiSetup s;
    s.model = "m";
    s.google_search = true;
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, build_setup(s)) == DeserializationError::Ok);
    CHECK((doc["setup"]["tools"].size() == 1));
    s.google_search = false;
    REQUIRE(deserializeJson(doc, build_setup(s)) == DeserializationError::Ok);
    CHECK(doc["setup"]["tools"].isNull());
}

TEST_CASE("language code maps short codes to BCP-47")
{
    CHECK(gemini_language_code("es") == "es-ES");
    CHECK(gemini_language_code("en") == "en-US");
    CHECK(gemini_language_code("pt") == "pt-BR");
    CHECK(gemini_language_code("fr") == "fr-FR");
    CHECK(gemini_language_code("de") == "de-DE");
    CHECK(gemini_language_code("it") == "it-IT");
    CHECK(gemini_language_code("ja-JP") == "ja-JP");
    CHECK(gemini_language_code("") == "en-US");
}

TEST_CASE("audio chunk mime is pcm 16k")
{
    int16_t pcm[4] = {0, 1, -1, 32767};
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, build_audio(pcm, 4)) == DeserializationError::Ok);
    CHECK((doc["realtimeInput"]["audio"]["mimeType"] == "audio/pcm;rate=16000"));
    std::vector<uint8_t> raw;
    REQUIRE(b64_decode(doc["realtimeInput"]["audio"]["data"].as<const char*>(), raw));
    REQUIRE(raw.size() == 8);
    CHECK(raw[2] == 0x01);
    CHECK(raw[3] == 0x00);
    CHECK(raw[4] == 0xFF);
    CHECK(raw[5] == 0xFF);
}

TEST_CASE("image chunk is jpeg video input")
{
    uint8_t jpeg[3] = {0xFF, 0xD8, 0xFF};
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, build_image(jpeg, 3)) == DeserializationError::Ok);
    CHECK((doc["realtimeInput"]["video"]["mimeType"] == "image/jpeg"));
    CHECK((doc["realtimeInput"]["video"]["data"] == "/9j/"));
}

TEST_CASE("tool response wraps id name and response object")
{
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, build_tool_response("call_1", "look", R"({"ok":true})")) == DeserializationError::Ok);
    CHECK((doc["toolResponse"]["functionResponses"][0]["id"] == "call_1"));
    CHECK((doc["toolResponse"]["functionResponses"][0]["name"] == "look"));
    CHECK((doc["toolResponse"]["functionResponses"][0]["response"]["ok"] == true));
    // invalid response JSON is wrapped as an error object rather than breaking the message
    REQUIRE(deserializeJson(doc, build_tool_response("c", "x", "not json")) == DeserializationError::Ok);
    CHECK(doc["toolResponse"]["functionResponses"][0]["response"]["error"].is<const char*>());
}

TEST_CASE("parses setupComplete")
{
    ServerMsg m = parse_server_message(chan_test::fixture("gemini_setup_complete.json"));
    CHECK(m.kind == ServerMsg::SetupComplete);
}

TEST_CASE("parses audio part into pcm bytes")
{
    ServerMsg m = parse_server_message(chan_test::fixture("gemini_audio_part.json"));
    CHECK(m.kind == ServerMsg::Audio);
    CHECK(m.sample_rate == 24000);
    CHECK(m.audio.size() > 0);
    CHECK(m.audio.size() % 2 == 0);
}

TEST_CASE("parses interrupted and turnComplete")
{
    ServerMsg t = parse_server_message(chan_test::fixture("gemini_turn_complete.json"));
    CHECK(t.kind == ServerMsg::TurnComplete);
    ServerMsg i = parse_server_message(R"({"serverContent":{"interrupted":true}})");
    CHECK(i.kind == ServerMsg::Interrupted);
}

TEST_CASE("parses output transcription text")
{
    // The real API sends the transcription alongside the audio part in the same message.
    ServerMsg m = parse_server_message(chan_test::fixture("gemini_output_transcription.json"));
    CHECK(m.kind == ServerMsg::Audio);
    CHECK_FALSE(m.output_text.empty());
    ServerMsg o = parse_server_message(R"({"serverContent":{"outputTranscription":{"text":"hola"}}})");
    CHECK(o.kind == ServerMsg::Transcription);
    CHECK(o.output_text == "hola");
    ServerMsg u = parse_server_message(R"({"serverContent":{"inputTranscription":{"text":"hola"}}})");
    CHECK(u.kind == ServerMsg::Transcription);
    CHECK(u.input_text == "hola");
}

TEST_CASE("parses tool call with two calls")
{
    ServerMsg m = parse_server_message(chan_test::fixture("gemini_tool_call.json"));
    REQUIRE(m.kind == ServerMsg::ToolCall);
    REQUIRE(m.calls.size() == 1);
    CHECK(m.calls[0].name == "look");
    CHECK(m.calls[0].id == "call_16979818093071475944");
    CHECK(m.calls[0].args_json == "{}");

    ServerMsg two = parse_server_message(
        R"({"toolCall":{"functionCalls":[{"id":"a","name":"set_head_angles","args":{"yaw":10,"pitch":-5}},{"id":"b","name":"dance","args":{}}]}})");
    REQUIRE(two.calls.size() == 2);
    CHECK(two.calls[0].args_json == R"({"yaw":10,"pitch":-5})");
    CHECK(two.calls[1].name == "dance");
}

TEST_CASE("parses goAway and unknown without crash")
{
    CHECK(parse_server_message(R"({"goAway":{"timeLeft":"10s"}})").kind == ServerMsg::GoAway);
    CHECK(parse_server_message(R"({"usageMetadata":{"promptTokenCount":1}})").kind == ServerMsg::Unknown);
    CHECK(parse_server_message("{}").kind == ServerMsg::Unknown);
    ServerMsg bad = parse_server_message("not json");
    CHECK(bad.kind == ServerMsg::Error);
    CHECK_FALSE(bad.error.empty());
}

TEST_CASE("system prompt contains name language and persona")
{
    ChanConfig cfg;
    cfg.name = "Chan";
    cfg.language = "es";
    cfg.persona_prompt = "Playful and kind.";
    cfg.chattiness = 0;
    std::string p = build_system_prompt(cfg);
    CHECK(p.find("Chan") != std::string::npos);
    CHECK(p.find("Playful and kind.") != std::string::npos);
    CHECK(p.find("Spanish") != std::string::npos);
    CHECK(p.find("one short sentence") != std::string::npos);
    cfg.chattiness = 2;
    CHECK(build_system_prompt(cfg).find("one short sentence") == std::string::npos);
}
