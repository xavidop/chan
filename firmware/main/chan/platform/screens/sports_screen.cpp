#include "sports_screen.h"

#include <algorithm>
#include <cstdio>
#include <ctime>

namespace chan::platform {

namespace {
constexpr uint32_t kFg = 0xF2F5F7;
constexpr uint32_t kMuted = 0x8A94A6;
constexpr uint32_t kRowBg = 0x1A2030;
constexpr uint32_t kLive = 0x3DDC84;
constexpr uint32_t kNeutralChip = 0x3A4150;

std::string when_text(uint64_t start_epoch, uint64_t now_epoch, bool final_mode)
{
    if (start_epoch == 0) return "";
    time_t t = static_cast<time_t>(start_epoch);
    struct tm tm_g;
    localtime_r(&t, &tm_g);
    char buf[24];
    if (final_mode) {
        strftime(buf, sizeof(buf), "%b %d", &tm_g);
        return buf;
    }
    // today: show the time; otherwise day + time
    time_t n = static_cast<time_t>(now_epoch);
    struct tm tm_n;
    localtime_r(&n, &tm_n);
    if (tm_n.tm_yday == tm_g.tm_yday && tm_n.tm_year == tm_g.tm_year) strftime(buf, sizeof(buf), "%H:%M", &tm_g);
    else strftime(buf, sizeof(buf), "%a %H:%M", &tm_g);
    return buf;
}

uint32_t chip_color(const Side& side, const Game& g, const std::vector<Team>& teams)
{
    Game only = g;
    if (&side == &g.home) only.away = Side{};
    else only.home = Side{};
    for (const auto& t : teams) {
        if (game_involves(only, t)) return t.color ? t.color : kNeutralChip;
    }
    return kNeutralChip;
}

std::string short_name(const Side& s)
{
    if (!s.abbrev.empty()) return s.abbrev;
    // last word of the name, capped
    std::string n = s.name;
    auto sp = n.find_last_of(' ');
    if (sp != std::string::npos && sp + 1 < n.size()) n = n.substr(sp + 1);
    if (n.size() > 10) n.resize(10);
    return n;
}

lv_obj_t* chip(lv_obj_t* row, uint32_t color, const std::string& text, int x)
{
    lv_obj_t* c = lv_obj_create(row);
    lv_obj_set_size(c, 78, 24);
    lv_obj_set_style_bg_color(c, lv_color_hex(color), 0);
    lv_obj_set_style_bg_opa(c, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(c, 0, 0);
    lv_obj_set_style_radius(c, 7, 0);
    lv_obj_set_style_pad_all(c, 0, 0);
    lv_obj_remove_flag(c, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_SCROLLABLE | LV_OBJ_FLAG_CLICKABLE));
    lv_obj_align(c, LV_ALIGN_LEFT_MID, x, 0);
    lv_obj_t* l = lv_label_create(c);
    lv_label_set_text(l, text.c_str());
    lv_obj_set_style_text_font(l, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(l, lv_color_hex(kFg), 0);
    lv_obj_center(l);
    return c;
}
}  // namespace

void SportsScreen::init(lv_obj_t* root)
{
    if (lv_obj_get_child_count(root) >= 2) _empty = lv_obj_get_child(root, 1);

    _subtitle = lv_label_create(root);
    lv_label_set_text(_subtitle, "");
    lv_obj_set_style_text_font(_subtitle, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(_subtitle, lv_color_hex(kMuted), 0);
    lv_obj_align(_subtitle, LV_ALIGN_TOP_RIGHT, -16, 20);

    _list = lv_obj_create(root);
    lv_obj_set_size(_list, 320 - 16, 240 - 50);
    lv_obj_align(_list, LV_ALIGN_TOP_LEFT, 8, 46);
    lv_obj_set_style_bg_opa(_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(_list, 0, 0);
    lv_obj_set_style_pad_all(_list, 0, 0);
    lv_obj_set_style_pad_row(_list, 6, 0);
    lv_obj_set_flex_flow(_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(_list, LV_DIR_VER);
    lv_obj_add_flag(_list, LV_OBJ_FLAG_GESTURE_BUBBLE);

    _banner = lv_obj_create(root);
    lv_obj_set_size(_banner, 320, 240);
    lv_obj_align(_banner, LV_ALIGN_CENTER, 0, 0);
    lv_obj_set_style_bg_opa(_banner, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(_banner, 0, 0);
    lv_obj_set_style_radius(_banner, 0, 0);
    lv_obj_remove_flag(_banner, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(_banner, static_cast<lv_obj_flag_t>(LV_OBJ_FLAG_HIDDEN | LV_OBJ_FLAG_GESTURE_BUBBLE));
    _banner_label = lv_label_create(_banner);
    lv_label_set_text(_banner_label, "");
    lv_obj_set_style_text_font(_banner_label, &lv_font_montserrat_24, 0);
    lv_obj_set_style_transform_scale(_banner_label, 512, 0);  // 2x
    lv_obj_set_style_text_color(_banner_label, lv_color_hex(kFg), 0);
    lv_obj_center(_banner_label);
}

void SportsScreen::add_row(const Game& g, const std::vector<Team>& teams, bool final_mode, uint64_t now_epoch)
{
    lv_obj_t* row = lv_obj_create(_list);
    lv_obj_set_size(row, 320 - 16, 44);
    lv_obj_set_style_bg_color(row, lv_color_hex(kRowBg), 0);
    lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(row, g.status == GameStatus::Live ? 2 : 0, 0);
    lv_obj_set_style_border_color(row, lv_color_hex(kLive), 0);
    lv_obj_set_style_radius(row, 10, 0);
    lv_obj_set_style_pad_all(row, 6, 0);
    lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);

    chip(row, chip_color(g.away, g, teams), short_name(g.away), 0);
    lv_obj_t* score = lv_label_create(row);
    char buf[24];
    if (g.status == GameStatus::Scheduled) snprintf(buf, sizeof(buf), "vs");
    else snprintf(buf, sizeof(buf), "%d - %d", g.away.score, g.home.score);
    lv_label_set_text(score, buf);
    lv_obj_set_style_text_font(score, &lv_font_montserrat_20, 0);
    lv_obj_set_style_text_color(score, lv_color_hex(kFg), 0);
    lv_obj_align(score, LV_ALIGN_LEFT_MID, 88, 0);
    chip(row, chip_color(g.home, g, teams), short_name(g.home), 150);

    lv_obj_t* status = lv_label_create(row);
    std::string st = g.status == GameStatus::Live ? g.clock : g.status == GameStatus::Final ? ("FT " + when_text(g.start_epoch, now_epoch, true))
                                                                                            : when_text(g.start_epoch, now_epoch, false);
    lv_label_set_text(status, st.c_str());
    lv_obj_set_style_text_font(status, &lv_font_montserrat_14, 0);
    lv_obj_set_style_text_color(status, lv_color_hex(g.status == GameStatus::Live ? kLive : kMuted), 0);
    lv_obj_align(status, LV_ALIGN_RIGHT_MID, -2, 0);
    (void)final_mode;
}

void SportsScreen::render(const std::vector<Game>& games, const std::map<std::string, Game>& last_results,
                          const std::vector<Team>& teams, uint64_t now_epoch)
{
    if (!_list) return;
    lv_obj_clean(_list);
    // "games" = current followed games (live/scheduled/final today). If none is live or upcoming,
    // show the latest result per team instead.
    std::vector<Game> active;
    for (const auto& g : games) {
        if (g.status != GameStatus::Final) active.push_back(g);
    }
    if (!active.empty()) {
        lv_label_set_text(_subtitle, active.front().status == GameStatus::Live ? "LIVE" : "today");
        for (const auto& g : active) add_row(g, teams, false, now_epoch);
        if (_empty) lv_obj_add_flag(_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    std::vector<Game> last;
    for (const auto& [key, g] : last_results) last.push_back(g);
    std::sort(last.begin(), last.end(), [](const Game& a, const Game& b) { return a.start_epoch > b.start_epoch; });
    if (last.empty()) {
        lv_label_set_text(_subtitle, "");
        if (_empty) lv_obj_remove_flag(_empty, LV_OBJ_FLAG_HIDDEN);
        return;
    }
    if (_empty) lv_obj_add_flag(_empty, LV_OBJ_FLAG_HIDDEN);
    lv_label_set_text(_subtitle, "last results");
    for (const auto& g : last) add_row(g, teams, true, now_epoch);
}

void SportsScreen::show_banner(const std::string& line, uint32_t color, uint32_t alt_color)
{
    if (!_banner) return;
    lv_obj_set_style_bg_color(_banner, lv_color_hex(color ? color : 0x1A2030), 0);
    lv_obj_set_style_text_color(_banner_label, lv_color_hex(alt_color ? alt_color : kFg), 0);
    lv_label_set_text(_banner_label, line.c_str());
    lv_obj_center(_banner_label);
    lv_obj_remove_flag(_banner, LV_OBJ_FLAG_HIDDEN);
    lv_obj_move_foreground(_banner);
}

void SportsScreen::hide_banner()
{
    if (_banner) lv_obj_add_flag(_banner, LV_OBJ_FLAG_HIDDEN);
}

}  // namespace chan::platform
