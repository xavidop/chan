// Location (from the public IP) and current weather (Open-Meteo). Pure C++17, host-tested.
#pragma once

#include <ArduinoJson.h>

#include <optional>
#include <string>
#include <string_view>

namespace chan {

struct Location {
    double lat = 0, lon = 0;
    std::string city;
    std::string timezone;  // IANA, e.g. "Europe/Madrid"
};

struct Weather {
    double temp = 0;   // current, in the requested unit
    int code = 0;      // WMO weather code
    bool is_day = true;
    double high = 0, low = 0;  // today's max/min
};

enum class WeatherIcon { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Storm };

const char* ipwho_url();  // http://ipwho.is/?fields=… (plain HTTP on purpose: public data, TLS RAM is reserved for Gemini)
std::string open_meteo_url(double lat, double lon, const std::string& units);  // units: "metric" | "imperial"

std::optional<Location> parse_ipwho(std::string_view json);
std::optional<Weather> parse_open_meteo(std::string_view json);
std::optional<Location> parse_ipwho(JsonVariantConst root);
std::optional<Weather> parse_open_meteo(JsonVariantConst root);

WeatherIcon icon_for_code(int wmo_code);
const char* condition_name(WeatherIcon icon);  // "clear", "partly cloudy", …

// IANA zone → POSIX TZ string for setenv("TZ"). Unknown zones → "UTC0".
std::string posix_tz_for(const std::string& iana);

// JSON for the get_weather tool.
std::string weather_json(const Location& loc, const Weather& w, const std::string& units, const std::string& local_time);

}  // namespace chan
