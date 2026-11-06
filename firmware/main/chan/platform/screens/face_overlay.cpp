#include "face_overlay.h"

#include <chan/platform/screens/weather_icon.h>

#include <cstdio>

namespace chan::platform {

namespace {
constexpr uint32_t kText = 0xE8ECF2;
constexpr uint32_t kMuted = 0x9AA3B2;
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
    lv_obj_remove_flag(_icon, LV_OBJ_FLAG_HIDDEN);
    weather_icon_draw(_icon, icon, is_day, 1);
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
