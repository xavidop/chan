// Small clock + weather shown with the face while Chan is idle. LVGL lock required.
#pragma once

#include <chan/core/weather.h>

#include <lvgl.h>

#include <optional>
#include <string>

namespace chan::platform {

class FaceOverlay {
public:
    void init(lv_obj_t* parent);
    void set_time(const std::string& hhmm);
    void set_weather(const std::optional<Weather>& w, const std::string& units);
    void set_visible(bool visible);

private:
    void draw_icon(WeatherIcon icon, bool is_day);

    lv_obj_t* _root = nullptr;
    lv_obj_t* _time = nullptr;
    lv_obj_t* _temp = nullptr;
    lv_obj_t* _icon = nullptr;
    std::string _last_time;
    bool _visible = true;
};

}  // namespace chan::platform
