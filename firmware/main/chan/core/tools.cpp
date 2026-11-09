#include "tools.h"

#include <ArduinoJson.h>

#include <exception>

namespace chan {

void ToolRegistry::add(FunctionDecl decl, ToolFn fn)
{
    for (auto& e : _tools) {
        if (e.decl.name == decl.name) {
            e.decl = std::move(decl);
            e.fn = std::move(fn);
            return;
        }
    }
    _tools.push_back({std::move(decl), std::move(fn)});
}

std::vector<FunctionDecl> ToolRegistry::declarations() const
{
    std::vector<FunctionDecl> out;
    out.reserve(_tools.size());
    for (const auto& e : _tools) out.push_back(e.decl);
    return out;
}

std::string ToolRegistry::dispatch(const std::string& name, const std::string& args_json) const
{
    for (const auto& e : _tools) {
        if (e.decl.name != name) continue;
        try {
            std::string result = e.fn(args_json);
            JsonDocument check;
            if (deserializeJson(check, result) != DeserializationError::Ok || !check.is<JsonObjectConst>()) {
                return error_json("tool " + name + " returned invalid JSON");
            }
            return result;
        } catch (const std::exception& ex) {
            return error_json(ex.what());
        } catch (...) {
            return error_json("tool " + name + " failed");
        }
    }
    return error_json("unknown tool " + name);
}

int arg_int(const std::string& args_json, const char* key, int fallback)
{
    JsonDocument doc;
    if (deserializeJson(doc, args_json) != DeserializationError::Ok) return fallback;
    JsonVariantConst v = doc[key];
    if (v.is<int>()) return v.as<int>();
    if (v.is<double>()) return static_cast<int>(v.as<double>());
    return fallback;
}

std::string arg_string(const std::string& args_json, const char* key, const std::string& fallback)
{
    JsonDocument doc;
    if (deserializeJson(doc, args_json) != DeserializationError::Ok) return fallback;
    JsonVariantConst v = doc[key];
    return v.is<const char*>() ? std::string(v.as<const char*>()) : fallback;
}

std::string error_json(const std::string& message)
{
    JsonDocument doc;
    doc["error"] = message;
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace chan
