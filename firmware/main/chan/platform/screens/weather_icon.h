// Tiny vector-ish weather icons drawn with LVGL shapes (no bitmap assets). LVGL lock required.
#pragma once

#include <chan/core/weather.h>

#include <lvgl.h>

namespace chan::platform {

// Creates an icon of about 22*scale x 18*scale px inside `parent` (fresh children each call).
void weather_icon_draw(lv_obj_t* parent, WeatherIcon icon, bool is_day, int scale = 1);

}  // namespace chan::platform
