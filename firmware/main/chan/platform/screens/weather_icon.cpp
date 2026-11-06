#include "weather_icon.h"

namespace chan::platform {

namespace {
constexpr uint32_t kSun = 0xFFC83C;
constexpr uint32_t kMoon = 0xD8DEE9;
constexpr uint32_t kCloud = 0xB8C0CC;
constexpr uint32_t kDarkCloud = 0x7A8494;
constexpr uint32_t kRain = 0x4FA3F7;
constexpr uint32_t kSnow = 0xFFFFFF;

lv_obj_t* shape(lv_obj_t* parent, int w, int h, uint32_t color, int radius, int x, int y, int k, lv_opa_t opa = LV_OPA_COVER)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, w * k, h * k);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius == LV_RADIUS_CIRCLE ? LV_RADIUS_CIRCLE : radius * k, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(o, x * k, y * k);
    return o;
}
}  // namespace

void weather_icon_draw(lv_obj_t* parent, WeatherIcon icon, bool is_day, int k)
{
    lv_obj_clean(parent);
    switch (icon) {
        case WeatherIcon::Clear:
            shape(parent, 14, 14, is_day ? kSun : kMoon, LV_RADIUS_CIRCLE, 3, 2, k);
            break;
        case WeatherIcon::PartlyCloudy:
            shape(parent, 11, 11, is_day ? kSun : kMoon, LV_RADIUS_CIRCLE, 1, 1, k);
            shape(parent, 15, 9, kCloud, 5, 7, 8, k);
            break;
        case WeatherIcon::Cloudy:
            shape(parent, 18, 10, kCloud, 5, 2, 5, k);
            break;
        case WeatherIcon::Fog:
            shape(parent, 18, 3, kCloud, 2, 2, 4, k, LV_OPA_70);
            shape(parent, 18, 3, kCloud, 2, 2, 9, k, LV_OPA_50);
            shape(parent, 18, 3, kCloud, 2, 2, 14, k, LV_OPA_30);
            break;
        case WeatherIcon::Drizzle:
        case WeatherIcon::Rain:
            shape(parent, 18, 9, kDarkCloud, 5, 2, 1, k);
            shape(parent, 3, 5, kRain, 2, 5, 12, k);
            shape(parent, 3, 5, kRain, 2, 11, 12, k);
            if (icon == WeatherIcon::Rain) shape(parent, 3, 5, kRain, 2, 17, 12, k);
            break;
        case WeatherIcon::Snow:
            shape(parent, 18, 9, kCloud, 5, 2, 1, k);
            shape(parent, 4, 4, kSnow, LV_RADIUS_CIRCLE, 5, 12, k);
            shape(parent, 4, 4, kSnow, LV_RADIUS_CIRCLE, 12, 13, k);
            break;
        case WeatherIcon::Storm:
            shape(parent, 18, 9, kDarkCloud, 5, 2, 1, k);
            shape(parent, 4, 7, kSun, 1, 9, 11, k);
            break;
    }
}

}  // namespace chan::platform
