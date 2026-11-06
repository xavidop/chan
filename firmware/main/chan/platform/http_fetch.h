// HTTPS GET → ArduinoJson document, for the free data sources (ipwho.is, Open-Meteo, ESPN, MLB).
#pragma once

#include <ArduinoJson.h>

#include <string>

namespace chan::platform {

// Fetches `url` (TLS via the IDF certificate bundle), buffers the body (PSRAM) up to `max_bytes`
// and parses it into `out`, applying `filter` when given (ArduinoJson DeserializationOption::Filter)
// so large feeds only keep the fields we need. Returns false with `error` set otherwise.
bool http_get_json(const std::string& url, JsonDocument& out, const JsonDocument* filter, std::string& error,
                   size_t max_bytes = 512 * 1024);

}  // namespace chan::platform
