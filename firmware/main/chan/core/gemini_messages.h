// Gemini Live API (BidiGenerateContent over WebSocket) message builders and parser.
// Pure C++17 + ArduinoJson, host-tested against fixtures captured from the real API
// (see scripts/gemini_live_probe.py).
#pragma once

#include <chan/core/config.h>

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chan {

constexpr const char* kGeminiLiveHost = "generativelanguage.googleapis.com";
constexpr const char* kGeminiLivePath = "/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent";
constexpr int kGeminiInputRate = 16000;
constexpr int kGeminiOutputRate = 24000;

struct FunctionDecl {
    std::string name;
    std::string description;
    std::string params_json_schema;  // Gemini OpenAPI-style schema, e.g. {"type":"OBJECT","properties":{}}
};

struct GeminiSetup {
    std::string model;          // e.g. "gemini-3.8-live" (without the "models/" prefix)
    std::string voice;          // prebuilt voice name, e.g. "Aoede"; empty → server default
    std::string language;       // "es" or "es-ES"; mapped with gemini_language_code()
    std::string system_prompt;
    bool google_search = true;
    std::vector<FunctionDecl> functions;
};

// "es" → "es-ES", "en" → "en-US", …; full BCP-47 codes pass through; empty → "en-US".
std::string gemini_language_code(std::string_view language);

// wss URL; the key is a query parameter. Never log the result unredacted.
std::string build_ws_url(std::string_view api_key);

std::string build_setup(const GeminiSetup& s);
std::string build_audio(const int16_t* pcm, size_t samples);           // realtimeInput.audio, 16 kHz PCM16
std::string build_image(const uint8_t* jpeg, size_t len);              // realtimeInput.video, image/jpeg
std::string build_tool_response(const std::string& id, const std::string& name, const std::string& response_json);

struct ServerMsg {
    enum Kind { SetupComplete, Audio, Interrupted, TurnComplete, ToolCall, GoAway, Transcription, Error, Unknown };
    struct Call {
        std::string id, name, args_json;
    };
    Kind kind = Unknown;
    std::vector<uint8_t> audio;  // raw PCM16 LE when kind == Audio
    int sample_rate = 0;         // from the mimeType, e.g. 24000
    std::string output_text;     // serverContent.outputTranscription.text (may accompany Audio)
    std::string input_text;      // serverContent.inputTranscription.text
    std::vector<Call> calls;     // kind == ToolCall
    std::string error;           // kind == Error
};

ServerMsg parse_server_message(std::string_view json);

// Persona + house rules from the config: name, language, chattiness (0 terse, 1 normal, 2 chatty).
std::string build_system_prompt(const ChanConfig& cfg);

}  // namespace chan
