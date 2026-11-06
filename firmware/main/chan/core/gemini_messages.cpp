#include "gemini_messages.h"

#include <ArduinoJson.h>
#include <chan/core/base64.h>

#include <cstdlib>
#include <cstring>

namespace chan {

namespace {

struct Lang {
    const char* code;
    const char* bcp47;
    const char* english_name;
};
constexpr Lang kLangs[] = {
    {"es", "es-ES", "Spanish"}, {"en", "en-US", "English"},    {"pt", "pt-BR", "Portuguese"},
    {"fr", "fr-FR", "French"},  {"de", "de-DE", "German"},     {"it", "it-IT", "Italian"},
    {"ja", "ja-JP", "Japanese"}, {"zh", "cmn-CN", "Mandarin Chinese"},
};

const Lang* find_lang(std::string_view language)
{
    std::string_view prefix = language.substr(0, language.find('-'));
    for (const auto& l : kLangs) {
        if (prefix == l.code) return &l;
    }
    return nullptr;
}

std::string serialize(const JsonDocument& doc)
{
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace

std::string gemini_language_code(std::string_view language)
{
    if (language.empty()) return "en-US";
    if (language.find('-') != std::string_view::npos) return std::string(language);
    if (const Lang* l = find_lang(language)) return l->bcp47;
    return std::string(language);
}

std::string build_ws_url(std::string_view api_key)
{
    return std::string("wss://") + kGeminiLiveHost + kGeminiLivePath + "?key=" + std::string(api_key);
}

std::string build_setup(const GeminiSetup& s)
{
    JsonDocument doc;
    JsonObject setup = doc["setup"].to<JsonObject>();
    setup["model"] = "models/" + s.model;

    JsonObject gen = setup["generationConfig"].to<JsonObject>();
    gen["responseModalities"].to<JsonArray>().add("AUDIO");
    JsonObject speech = gen["speechConfig"].to<JsonObject>();
    if (!s.voice.empty()) speech["voiceConfig"]["prebuiltVoiceConfig"]["voiceName"] = s.voice;
    speech["languageCode"] = gemini_language_code(s.language);

    if (!s.system_prompt.empty()) {
        setup["systemInstruction"]["parts"].to<JsonArray>().add<JsonObject>()["text"] = s.system_prompt;
    }

    if (s.google_search || !s.functions.empty()) {
        JsonArray tools = setup["tools"].to<JsonArray>();
        if (s.google_search) tools.add<JsonObject>()["googleSearch"].to<JsonObject>();
        if (!s.functions.empty()) {
            JsonArray decls = tools.add<JsonObject>()["functionDeclarations"].to<JsonArray>();
            for (const auto& f : s.functions) {
                JsonObject d = decls.add<JsonObject>();
                d["name"] = f.name;
                d["description"] = f.description;
                JsonDocument params;
                if (!f.params_json_schema.empty() && deserializeJson(params, f.params_json_schema) == DeserializationError::Ok) {
                    d["parameters"] = params;
                }
            }
        }
    }

    setup["inputAudioTranscription"].to<JsonObject>();
    setup["outputAudioTranscription"].to<JsonObject>();
    return serialize(doc);
}

std::string build_audio(const int16_t* pcm, size_t samples)
{
    JsonDocument doc;
    JsonObject audio = doc["realtimeInput"]["audio"].to<JsonObject>();
    audio["mimeType"] = "audio/pcm;rate=16000";
    audio["data"] = b64_encode(reinterpret_cast<const uint8_t*>(pcm), samples * sizeof(int16_t));
    return serialize(doc);
}

std::string build_image(const uint8_t* jpeg, size_t len)
{
    JsonDocument doc;
    JsonObject video = doc["realtimeInput"]["video"].to<JsonObject>();
    video["mimeType"] = "image/jpeg";
    video["data"] = b64_encode(jpeg, len);
    return serialize(doc);
}

std::string build_tool_response(const std::string& id, const std::string& name, const std::string& response_json)
{
    JsonDocument doc;
    JsonObject fr = doc["toolResponse"]["functionResponses"].to<JsonArray>().add<JsonObject>();
    fr["id"] = id;
    fr["name"] = name;
    JsonDocument resp;
    if (deserializeJson(resp, response_json) == DeserializationError::Ok && resp.is<JsonObjectConst>()) {
        fr["response"] = resp;
    } else {
        fr["response"]["error"] = "tool returned invalid JSON";
    }
    return serialize(doc);
}

ServerMsg parse_server_message(std::string_view json)
{
    ServerMsg m;
    JsonDocument doc;
    DeserializationError err = deserializeJson(doc, json.data(), json.size());
    if (err) {
        m.kind = ServerMsg::Error;
        m.error = std::string("invalid server JSON: ") + err.c_str();
        return m;
    }

    if (doc["setupComplete"].is<JsonObjectConst>()) {
        m.kind = ServerMsg::SetupComplete;
        return m;
    }
    if (doc["goAway"].is<JsonObjectConst>()) {
        m.kind = ServerMsg::GoAway;
        return m;
    }
    if (JsonArrayConst calls = doc["toolCall"]["functionCalls"].as<JsonArrayConst>(); !calls.isNull()) {
        m.kind = ServerMsg::ToolCall;
        for (JsonObjectConst c : calls) {
            ServerMsg::Call call;
            call.id = c["id"].as<const char*>() ? c["id"].as<const char*>() : "";
            call.name = c["name"].as<const char*>() ? c["name"].as<const char*>() : "";
            if (c["args"].isNull()) {
                call.args_json = "{}";
            } else {
                serializeJson(c["args"], call.args_json);
            }
            m.calls.push_back(std::move(call));
        }
        return m;
    }

    JsonObjectConst sc = doc["serverContent"].as<JsonObjectConst>();
    if (sc.isNull()) {
        m.kind = ServerMsg::Unknown;
        return m;
    }
    if (sc["outputTranscription"]["text"].is<const char*>()) m.output_text = sc["outputTranscription"]["text"].as<const char*>();
    if (sc["inputTranscription"]["text"].is<const char*>()) m.input_text = sc["inputTranscription"]["text"].as<const char*>();

    for (JsonObjectConst part : sc["modelTurn"]["parts"].as<JsonArrayConst>()) {
        JsonObjectConst inline_data = part["inlineData"].as<JsonObjectConst>();
        if (inline_data.isNull()) continue;
        const char* mime = inline_data["mimeType"].as<const char*>();
        const char* data = inline_data["data"].as<const char*>();
        if (!mime || !data || std::strncmp(mime, "audio/", 6) != 0) continue;
        std::vector<uint8_t> bytes;
        if (!b64_decode(data, bytes)) continue;
        m.audio.insert(m.audio.end(), bytes.begin(), bytes.end());
        if (const char* rate = std::strstr(mime, "rate=")) m.sample_rate = std::atoi(rate + 5);
    }
    if (!m.audio.empty()) {
        m.kind = ServerMsg::Audio;
        if (m.sample_rate == 0) m.sample_rate = kGeminiOutputRate;
        return m;
    }
    if (sc["interrupted"].as<bool>()) {
        m.kind = ServerMsg::Interrupted;
        return m;
    }
    if (sc["turnComplete"].as<bool>()) {
        m.kind = ServerMsg::TurnComplete;
        return m;
    }
    if (!m.output_text.empty() || !m.input_text.empty()) {
        m.kind = ServerMsg::Transcription;
        return m;
    }
    m.kind = ServerMsg::Unknown;
    return m;
}

std::string build_system_prompt(const ChanConfig& cfg)
{
    const Lang* l = find_lang(cfg.language);
    const std::string lang_name = l ? l->english_name : cfg.language;

    std::string p;
    p += "You are " + cfg.name + ", a small, cheerful desk robot (a Stack-chan) living on your owner's desk. ";
    if (!cfg.persona_prompt.empty()) p += cfg.persona_prompt + " ";
    p += "You speak " + lang_name + " unless the user clearly speaks another language. ";
    p += "You are talking out loud through a tiny speaker, so never use lists, markdown or long explanations. ";
    if (cfg.chattiness <= 0) {
        p += "Answer in one short sentence unless the user explicitly asks for detail. ";
    } else if (cfg.chattiness == 1) {
        p += "Keep replies to one or two short sentences unless the user asks for more. ";
    } else {
        p += "You may chat a little, but stay concise and warm. ";
    }
    p += "When you need to see something, use the camera tool. When you need current facts, search the web. ";
    p += "You can move your head, change your expression and dance with your tools; use them sparingly and playfully.";
    return p;
}

}  // namespace chan
