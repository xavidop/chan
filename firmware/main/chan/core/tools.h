// Tool registry for Gemini function calling. Pure C++17, host-tested.
#pragma once

#include <chan/core/gemini_messages.h>

#include <functional>
#include <string>
#include <vector>

namespace chan {

// Receives the call's args as a JSON object string; returns a JSON object string.
using ToolFn = std::function<std::string(const std::string& args_json)>;

class ToolRegistry {
public:
    // Adding a tool whose name already exists replaces it (later tasks fill in placeholders).
    void add(FunctionDecl decl, ToolFn fn);
    std::vector<FunctionDecl> declarations() const;
    // Unknown tool → {"error":"unknown tool <name>"}; a throwing tool or one returning
    // something that is not a JSON object → {"error":"…"}.
    std::string dispatch(const std::string& name, const std::string& args_json) const;

private:
    struct Entry {
        FunctionDecl decl;
        ToolFn fn;
    };
    std::vector<Entry> _tools;
};

// Small helpers for tool bodies.
int arg_int(const std::string& args_json, const char* key, int fallback);
std::string arg_string(const std::string& args_json, const char* key, const std::string& fallback);
std::string error_json(const std::string& message);

}  // namespace chan
