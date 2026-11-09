// Shared helpers for Chan host tests (header-only, test code only).
#pragma once

#include <ArduinoJson.h>

#include <fstream>
#include <sstream>
#include <string>

namespace chan_test {

inline std::string read_file(const std::string& path)
{
    std::ifstream in(path, std::ios::binary);
    if (!in) throw std::runtime_error("cannot open " + path);
    std::stringstream ss;
    ss << in.rdbuf();
    return ss.str();
}

inline std::string fixture(const std::string& name)
{
    return read_file(std::string(CHAN_FIXTURES_DIR) + "/" + name);
}

// Build the config blob the device receives: {"env": {...}, "config": {...}}
// from the committed example files, the same shape scripts/chan.py pack-config emits.
inline std::string example_blob()
{
    JsonDocument doc;
    JsonObject env = doc["env"].to<JsonObject>();
    std::istringstream lines(read_file(std::string(CHAN_REPO_ROOT) + "/.env.example"));
    std::string line;
    while (std::getline(lines, line)) {
        if (line.empty() || line[0] == '#') continue;
        auto eq = line.find('=');
        if (eq == std::string::npos) continue;
        env[line.substr(0, eq)] = line.substr(eq + 1);
    }
    JsonDocument cfg;
    deserializeJson(cfg, read_file(std::string(CHAN_REPO_ROOT) + "/config.example.json"));
    doc["config"] = cfg;
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace chan_test
