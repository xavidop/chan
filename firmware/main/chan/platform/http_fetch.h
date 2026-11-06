// HTTPS GET → ArduinoJson document, for the free data sources (ipwho.is, Open-Meteo, ESPN, MLB).
#pragma once

#include <ArduinoJson.h>

#include <string>

namespace chan::platform {

// ArduinoJson allocator backed by PSRAM so large feeds never eat the scarce internal RAM
// the TLS stack needs. Use: `JsonDocument doc(psram_allocator());`
ArduinoJson::Allocator* psram_allocator();

// Fetches `url` (http:// or https:// — the data feeds use plain HTTP on purpose; see docs/architecture.md), buffers the body (PSRAM) up to `max_bytes`
// and parses it into `out`, applying `filter` when given (ArduinoJson DeserializationOption::Filter)
// so large feeds only keep the fields we need. Returns false with `error` set otherwise.
bool http_get_json(const std::string& url, JsonDocument& out, const JsonDocument* filter, std::string& error,
                   size_t max_bytes = 512 * 1024);

}  // namespace chan::platform
