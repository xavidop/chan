#include "weather_screen.h"

#include <chan/platform/screens/weather_icon.h>

#include <cstdio>

namespace chan::platform {

namespace {
constexpr uint32_t kFg = 0xF2F5F7;
constexpr uint32_t kMuted = 0x8A94A6;
constexpr uint32_t kAccent = 0x4FA3F7;

lv_obj_t* label(lv_obj_t* parent, const lv_font_t* font, uint32_t color)
{
    lv_obj_t* l = lv_label_create(parent);
    lv_label_set_text(l, "");
    lv_obj_set_style_text_font(l, font, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(color), 0);
    return l;
}
}  // namespace

void WeatherScreen::init(lv_obj_t* root)
{
    if (lv_obj_get_child_count(root) >= 2) _empty = lv_obj_get_child(root, 1);

    _time = label(root, &lv_font_montserrat_16, kMuted);
    lv_obj_align(_time, LV_ALIGN_TOP_RIGHT, -16, 18);

    _icon = lv_obj_create(root);
    lv_obj_set_size(_icon, 66, 54);
    lv_obj_set_style_bg_opa(_icon, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_icon, 0, 0);
    lv_obj_set_style_pad_all(_icon, 0, 0);
    lv_obj_remove_flag(_icon, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_align(_icon, LV_ALIGN_LEFT_MID, 20, -18);

    _temp = label(root, &lv_font_montserrat_24, kFg);
    lv_obj_set_style_transform_scale(_temp, 384, 0);  // 1.5x of the 24 px font
    lv_obj_set_style_transform_pivot_x(_temp, 0, 0);
    lv_obj_set_style_transform_pivot_y(_temp, 0, 0);
    lv_obj_align(_temp, LV_ALIGN_LEFT_MID, 110, -36);

    _cond = label(root, &lv_font_montserrat_20, kAccent);
    lv_obj_align(_cond, LV_ALIGN_LEFT_MID, 110, 14);

    _range = label(root, &lv_font_montserrat_16, kMuted);
    lv_obj_align(_range, LV_ALIGN_LEFT_MID, 110, 40);

    _city = label(root, &lv_font_montserrat_16, kMuted);
    lv_obj_align(_city, LV_ALIGN_BOTTOM_LEFT, 20, -14);
}

void WeatherScreen::render(const std::optional<Location>& loc, const std::optional<Weather>& w, const std::string& units,
                           const std::string& hhmm)
{
    if (!_temp) return;
    lv_label_set_text(_time, hhmm.c_str());
    if (!w) {
        if (_empty) lv_obj_remove_flag(_empty, LV_OBJ_FLAG_HIDDEN);
        lv_label_set_text(_temp, "");
        lv_label_set_text(_cond, "");
        lv_label_set_text(_range, "");
        lv_label_set_text(_city, loc ? loc->city.c_str() : "");
        lv_obj_add_flag(_icon, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (_empty) lv_obj_add_flag(_empty, LV_OBJ_FLAG_HIDDEN);
    const char* u = units == "imperial" ? "F" : "C";
    char buf[48];
    snprintf(buf, sizeof(buf), "%.0f°%s", w->temp, u);
    lv_label_set_text(_temp, buf);
    lv_label_set_text(_cond, condition_name(icon_for_code(w->code)));
    snprintf(buf, sizeof(buf), "H %.0f°   L %.0f°", w->high, w->low);
    lv_label_set_text(_range, buf);
    lv_label_set_text(_city, loc ? loc->city.c_str() : "");
    const WeatherIcon icon = icon_for_code(w->code);
    if (w->code != _last_code || w->is_day != _last_day) {
        _last_code = w->code;
        _last_day = w->is_day;
        weather_icon_draw(_icon, icon, w->is_day, 3);
    }
    lv_obj_remove_flag(_icon, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace chan::platform
