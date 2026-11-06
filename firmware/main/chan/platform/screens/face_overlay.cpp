#include "face_overlay.h"

#include <cstdio>

namespace chan::platform {

namespace {
constexpr uint32_t kText = 0xE8ECF2;
constexpr uint32_t kMuted = 0x9AA3B2;
constexpr uint32_t kSun = 0xFFC83C;
constexpr uint32_t kMoon = 0xD8DEE9;
constexpr uint32_t kCloud = 0xB8C0CC;
constexpr uint32_t kDarkCloud = 0x7A8494;
constexpr uint32_t kRain = 0x4FA3F7;
constexpr uint32_t kSnow = 0xFFFFFF;

lv_obj_t* shape(lv_obj_t* parent, int w, int h, uint32_t color, int radius, int x, int y, lv_opa_t opa = LV_OPA_COVER)
{
    lv_obj_t* o = lv_obj_create(parent);
    lv_obj_set_size(o, w, h);
    lv_obj_set_style_bg_color(o, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(o, opa, 0);
    lv_obj_set_style_border_width(o, 0, 0);
    lv_obj_set_style_radius(o, radius, 0);
    lv_obj_set_style_pad_all(o, 0, 0);
    lv_obj_remove_flag(o, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(o, x, y);
    return o;
}
}  // namespace

void FaceOverlay::init(lv_obj_t* parent)
{
    _root = lv_obj_create(parent);
    lv_obj_set_size(_root, 110, 52);
    lv_obj_align(_root, LV_ALIGN_BOTTOM_LEFT, 8, -6);
    lv_obj_set_style_bg_opa(_root, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_root, 0, 0);
    lv_obj_set_style_pad_all(_root, 0, 0);
    lv_obj_remove_flag(_root, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_add_flag(_root, LV_OBJ_FLAG_GESTURE_BUBBLE);

    _time = lv_label_create(_root);
    lv_label_set_text(_time, "--:--");
    lv_obj_set_style_text_font(_time, &lv_font_montserrat_24, 0);
    lv_obj_set_style_text_color(_time, lv_color_hex(kText), 0);
    lv_obj_set_pos(_time, 0, 0);

    _icon = lv_obj_create(_root);
    lv_obj_set_size(_icon, 22, 18);
    lv_obj_set_style_bg_opa(_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_icon, 0, 0);
    lv_obj_set_style_pad_all(_icon, 0, 0);
    lv_obj_remove_flag(_icon, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_set_pos(_icon, 0, 31);
    lv_obj_add_flag(_icon, LV_OBJ_FLAG_HIDDEN);

    _temp = lv_label_create(_root);
    lv_label_set_text(_temp, "");
    lv_obj_set_style_text_font(_temp, &lv_font_montserrat_16, 0);
    lv_obj_set_style_text_color(_temp, lv_color_hex(kMuted), 0);
    lv_obj_set_pos(_temp, 28, 32);
}

void FaceOverlay::set_time(const std::string& hhmm)
{
    if (!_time || hhmm == _last_time) return;
    _last_time = hhmm;
    lv_label_set_text(_time, hhmm.c_str());
}

void FaceOverlay::draw_icon(WeatherIcon icon, bool is_day)
{
    lv_obj_clean(_icon);
    lv_obj_remove_flag(_icon, LV_OBJ_FLAG_HIDDEN);
    switch (icon) {
        case WeatherIcon::Clear:
            shape(_icon, 14, 14, is_day ? kSun : kMoon, LV_RADIUS_CIRCLE, 3, 2);
            break;
        case WeatherIcon::PartlyCloudy:
            shape(_icon, 11, 11, is_day ? kSun : kMoon, LV_RADIUS_CIRCLE, 1, 1);
            shape(_icon, 15, 9, kCloud, 5, 7, 8);
            break;
        case WeatherIcon::Cloudy:
            shape(_icon, 18, 10, kCloud, 5, 2, 5);
            break;
        case WeatherIcon::Fog:
            shape(_icon, 18, 3, kCloud, 2, 2, 4, LV_OPA_70);
            shape(_icon, 18, 3, kCloud, 2, 2, 9, LV_OPA_50);
            shape(_icon, 18, 3, kCloud, 2, 2, 14, LV_OPA_30);
            break;
        case WeatherIcon::Drizzle:
        case WeatherIcon::Rain:
            shape(_icon, 18, 9, kDarkCloud, 5, 2, 1);
            shape(_icon, 3, 5, kRain, 2, 5, 12);
            shape(_icon, 3, 5, kRain, 2, 11, 12);
            if (icon == WeatherIcon::Rain) shape(_icon, 3, 5, kRain, 2, 17, 12);
            break;
        case WeatherIcon::Snow:
            shape(_icon, 18, 9, kCloud, 5, 2, 1);
            shape(_icon, 4, 4, kSnow, LV_RADIUS_CIRCLE, 5, 12);
            shape(_icon, 4, 4, kSnow, LV_RADIUS_CIRCLE, 12, 13);
            break;
        case WeatherIcon::Storm:
            shape(_icon, 18, 9, kDarkCloud, 5, 2, 1);
            shape(_icon, 4, 7, kSun, 1, 9, 11);
            break;
    }
}

void FaceOverlay::set_weather(const std::optional<Weather>& w, const std::string& units)
{
    if (!_temp) return;
    if (!w) {
        lv_label_set_text(_temp, "");
        lv_obj_add_flag(_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    char buf[16];
    snprintf(buf, sizeof(buf), "%.0f°%s", w->temp, units == "imperial" ? "F" : "C");
    lv_label_set_text(_temp, buf);
    draw_icon(icon_for_code(w->code), w->is_day);
}

void FaceOverlay::set_visible(bool visible)
{
    if (!_root || visible == _visible) return;
    _visible = visible;
    if (visible) lv_obj_remove_flag(_root, LV_OBJ_FLAG_HIDDEN);
    else lv_obj_add_flag(_root, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace chan::platform
