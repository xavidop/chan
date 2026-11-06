#include <doctest.h>

#include <chan/core/weather.h>

#include "test_support.h"

using namespace chan;

TEST_CASE("parses ipwho fixture")
{
    auto loc = parse_ipwho(chan_test::fixture("ipwho.json"));
    REQUIRE(loc.has_value());
    CHECK(loc->city == "Valencia");
    CHECK(loc->lat == doctest::Approx(39.4699));
    CHECK(loc->lon == doctest::Approx(-0.3763));
    CHECK(loc->timezone == "Europe/Madrid");
}

TEST_CASE("ipwho success false is nullopt")
{
    CHECK_FALSE(parse_ipwho(R"({"success":false,"message":"rate limit"})").has_value());
    CHECK_FALSE(parse_ipwho("garbage").has_value());
    CHECK_FALSE(parse_ipwho(R"({"success":true,"city":"X"})").has_value());  // no coordinates
}

TEST_CASE("parses open-meteo fixture")
{
    auto w = parse_open_meteo(chan_test::fixture("open_meteo.json"));
    REQUIRE(w.has_value());
    CHECK(w->temp == doctest::Approx(18.3));
    CHECK(w->code == 2);
    CHECK(w->is_day);
    CHECK(w->high == doctest::Approx(21.7));
    CHECK(w->low == doctest::Approx(12.1));
}

TEST_CASE("open-meteo without current block is nullopt")
{
    CHECK_FALSE(parse_open_meteo(R"({"daily":{"temperature_2m_max":[1]}})").has_value());
    CHECK_FALSE(parse_open_meteo("{").has_value());
}

TEST_CASE("wmo codes map to icons")
{
    CHECK(icon_for_code(0) == WeatherIcon::Clear);
    CHECK(icon_for_code(1) == WeatherIcon::Clear);
    CHECK(icon_for_code(2) == WeatherIcon::PartlyCloudy);
    CHECK(icon_for_code(3) == WeatherIcon::Cloudy);
    CHECK(icon_for_code(45) == WeatherIcon::Fog);
    CHECK(icon_for_code(48) == WeatherIcon::Fog);
    CHECK(icon_for_code(51) == WeatherIcon::Drizzle);
    CHECK(icon_for_code(61) == WeatherIcon::Rain);
    CHECK(icon_for_code(80) == WeatherIcon::Rain);
    CHECK(icon_for_code(71) == WeatherIcon::Snow);
    CHECK(icon_for_code(95) == WeatherIcon::Storm);
    CHECK(icon_for_code(999) == WeatherIcon::Cloudy);
}

TEST_CASE("request urls carry coordinates and units")
{
    std::string u = open_meteo_url(39.47, -0.376, "metric");
    CHECK(u.rfind("https://api.open-meteo.com/v1/forecast?", 0) == 0);
    CHECK(u.find("latitude=39.47") != std::string::npos);
    CHECK(u.find("longitude=-0.376") != std::string::npos);
    CHECK(u.find("temperature_unit=fahrenheit") == std::string::npos);
    CHECK(open_meteo_url(1, 2, "imperial").find("temperature_unit=fahrenheit") != std::string::npos);
    CHECK(std::string(ipwho_url()).find("fields=success,city,latitude,longitude,timezone") != std::string::npos);
}

TEST_CASE("iana timezone maps to posix tz")
{
    CHECK(posix_tz_for("Europe/Madrid") == "CET-1CEST,M3.5.0,M10.5.0/3");
    CHECK(posix_tz_for("America/Toronto") == "EST5EDT,M3.2.0,M11.1.0");
    CHECK(posix_tz_for("Asia/Tokyo") == "JST-9");
    CHECK(posix_tz_for("Nowhere/Unknown") == "UTC0");
}

TEST_CASE("weather json for the model")
{
    Weather w{18.3, 2, true, 21.7, 12.1};
    Location l{39.47, -0.376, "Valencia", "Europe/Madrid"};
    JsonDocument doc;
    REQUIRE(deserializeJson(doc, weather_json(l, w, "metric", "14:05")) == DeserializationError::Ok);
    CHECK((doc["city"] == "Valencia"));
    CHECK(doc["temperature"].as<double>() == doctest::Approx(18.3));
    CHECK((doc["unit"] == "C"));
    CHECK((doc["condition"] == "partly cloudy"));
    CHECK((doc["local_time"] == "14:05"));
}
