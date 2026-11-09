// The Weather overlay: city, big temperature, condition, today's high/low, local time. LVGL lock required.
#pragma once

#include <chan/core/weather.h>

#include <lvgl.h>

#include <optional>
#include <string>

namespace chan::platform {

class WeatherScreen {
public:
    void init(lv_obj_t* root);  // the Shell's weather overlay (title already drawn)
    void render(const std::optional<Location>& loc, const std::optional<Weather>& w, const std::string& units,
                const std::string& hhmm);

private:
    lv_obj_t* _city = nullptr;
    lv_obj_t* _temp = nullptr;
    lv_obj_t* _cond = nullptr;
    lv_obj_t* _range = nullptr;
    lv_obj_t* _time = nullptr;
    lv_obj_t* _icon = nullptr;
    lv_obj_t* _empty = nullptr;
    int _last_code = -1000;
    bool _last_day = true;
};

}  // namespace chan::platform
