#include "weather.h"

#include <ArduinoJson.h>

#include <cstdio>

namespace chan {

const char* ipwho_url()
{
    return "http://ipwho.is/?fields=success,city,latitude,longitude,timezone";
}

std::string open_meteo_url(double lat, double lon, const std::string& units)
{
    char buf[256];
    snprintf(buf, sizeof(buf),
             "http://api.open-meteo.com/v1/forecast?latitude=%.4g&longitude=%.4g"
             "&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min"
             "&timezone=auto&forecast_days=1",
             lat, lon);
    std::string url = buf;
    if (units == "imperial") url += "&temperature_unit=fahrenheit";
    return url;
}

std::optional<Location> parse_ipwho(std::string_view json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) != DeserializationError::Ok) return std::nullopt;
    return parse_ipwho(doc.as<JsonVariantConst>());
}

std::optional<Weather> parse_open_meteo(std::string_view json)
{
    JsonDocument doc;
    if (deserializeJson(doc, json.data(), json.size()) != DeserializationError::Ok) return std::nullopt;
    return parse_open_meteo(doc.as<JsonVariantConst>());
}

std::optional<Location> parse_ipwho(JsonVariantConst doc)
{
    if (!doc["success"].as<bool>()) return std::nullopt;
    if (!doc["latitude"].is<double>() || !doc["longitude"].is<double>()) return std::nullopt;
    Location loc;
    loc.lat = doc["latitude"].as<double>();
    loc.lon = doc["longitude"].as<double>();
    loc.city = doc["city"].is<const char*>() ? doc["city"].as<const char*>() : "";
    JsonVariantConst tz = doc["timezone"];
    if (tz.is<const char*>()) loc.timezone = tz.as<const char*>();
    else if (tz["id"].is<const char*>()) loc.timezone = tz["id"].as<const char*>();
    return loc;
}

std::optional<Weather> parse_open_meteo(JsonVariantConst doc)
{
    JsonObjectConst cur = doc["current"].as<JsonObjectConst>();
    if (cur.isNull() || !cur["temperature_2m"].is<double>()) return std::nullopt;
    Weather w;
    w.temp = cur["temperature_2m"].as<double>();
    w.code = cur["weather_code"].as<int>();
    w.is_day = cur["is_day"].as<int>() != 0;
    JsonObjectConst daily = doc["daily"].as<JsonObjectConst>();
    if (!daily.isNull()) {
        w.high = daily["temperature_2m_max"][0].as<double>();
        w.low = daily["temperature_2m_min"][0].as<double>();
    }
    return w;
}

WeatherIcon icon_for_code(int c)
{
    if (c <= 1) return WeatherIcon::Clear;
    if (c == 2) return WeatherIcon::PartlyCloudy;
    if (c == 3) return WeatherIcon::Cloudy;
    if (c == 45 || c == 48) return WeatherIcon::Fog;
    if (c >= 51 && c <= 57) return WeatherIcon::Drizzle;
    if ((c >= 61 && c <= 67) || (c >= 80 && c <= 82)) return WeatherIcon::Rain;
    if ((c >= 71 && c <= 77) || c == 85 || c == 86) return WeatherIcon::Snow;
    if (c >= 95 && c <= 99) return WeatherIcon::Storm;
    return WeatherIcon::Cloudy;
}

const char* condition_name(WeatherIcon icon)
{
    switch (icon) {
        case WeatherIcon::Clear: return "clear";
        case WeatherIcon::PartlyCloudy: return "partly cloudy";
        case WeatherIcon::Cloudy: return "cloudy";
        case WeatherIcon::Fog: return "fog";
        case WeatherIcon::Drizzle: return "drizzle";
        case WeatherIcon::Rain: return "rain";
        case WeatherIcon::Snow: return "snow";
        case WeatherIcon::Storm: return "thunderstorm";
    }
    return "unknown";
}

std::string posix_tz_for(const std::string& iana)
{
    struct Zone {
        const char* iana;
        const char* posix;
    };
    // Common zones; extend as needed. Format: std offset [dst[offset],start,end]
    static const Zone kZones[] = {
        {"Europe/Madrid", "CET-1CEST,M3.5.0,M10.5.0/3"},     {"Europe/Paris", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Berlin", "CET-1CEST,M3.5.0,M10.5.0/3"},     {"Europe/Rome", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Amsterdam", "CET-1CEST,M3.5.0,M10.5.0/3"},  {"Europe/Brussels", "CET-1CEST,M3.5.0,M10.5.0/3"},
        {"Europe/Lisbon", "WET0WEST,M3.5.0/1,M10.5.0"},       {"Europe/London", "GMT0BST,M3.5.0/1,M10.5.0"},
        {"Europe/Dublin", "IST-1GMT0,M10.5.0,M3.5.0/1"},      {"Europe/Athens", "EET-2EEST,M3.5.0/3,M10.5.0/4"},
        {"Europe/Helsinki", "EET-2EEST,M3.5.0/3,M10.5.0/4"},  {"Europe/Moscow", "MSK-3"},
        {"America/Toronto", "EST5EDT,M3.2.0,M11.1.0"},        {"America/New_York", "EST5EDT,M3.2.0,M11.1.0"},
        {"America/Chicago", "CST6CDT,M3.2.0,M11.1.0"},        {"America/Denver", "MST7MDT,M3.2.0,M11.1.0"},
        {"America/Phoenix", "MST7"},                           {"America/Los_Angeles", "PST8PDT,M3.2.0,M11.1.0"},
        {"America/Vancouver", "PST8PDT,M3.2.0,M11.1.0"},      {"America/Mexico_City", "CST6"},
        {"America/Sao_Paulo", "<-03>3"},                       {"America/Argentina/Buenos_Aires", "<-03>3"},
        {"America/Bogota", "<-05>5"},                          {"America/Lima", "<-05>5"},
        {"America/Santiago", "<-04>4<-03>,M9.1.6/24,M4.1.6/24"}, {"Asia/Tokyo", "JST-9"},
        {"Asia/Seoul", "KST-9"},                               {"Asia/Shanghai", "CST-8"},
        {"Asia/Hong_Kong", "HKT-8"},                           {"Asia/Singapore", "<+08>-8"},
        {"Asia/Kolkata", "IST-5:30"},                          {"Asia/Dubai", "<+04>-4"},
        {"Australia/Sydney", "AEST-10AEDT,M10.1.0,M4.1.0/3"},  {"Australia/Melbourne", "AEST-10AEDT,M10.1.0,M4.1.0/3"},
        {"Australia/Perth", "AWST-8"},                         {"Pacific/Auckland", "NZST-12NZDT,M9.5.0,M4.1.0/3"},
        {"Atlantic/Canary", "WET0WEST,M3.5.0/1,M10.5.0"},      {"Africa/Johannesburg", "SAST-2"},
        {"Africa/Cairo", "EET-2EEST,M4.5.5/0,M10.5.4/24"},      {"UTC", "UTC0"},
        {"Etc/UTC", "UTC0"},
    };
    for (const auto& z : kZones) {
        if (iana == z.iana) return z.posix;
    }
    return "UTC0";
}

std::string weather_json(const Location& loc, const Weather& w, const std::string& units, const std::string& local_time)
{
    JsonDocument doc;
    doc["city"] = loc.city;
    doc["temperature"] = w.temp;
    doc["high"] = w.high;
    doc["low"] = w.low;
    doc["unit"] = units == "imperial" ? "F" : "C";
    doc["condition"] = condition_name(icon_for_code(w.code));
    doc["is_day"] = w.is_day;
    doc["local_time"] = local_time;
    doc["timezone"] = loc.timezone;
    std::string out;
    serializeJson(doc, out);
    return out;
}

}  // namespace chan
