// The Sports overlay: followed teams' games (live first), or their last results when nothing is
// on; plus a big celebration banner ("GOAL!"). LVGL lock required.
#pragma once

#include <chan/core/config.h>
#include <chan/core/sports.h>

#include <lvgl.h>

#include <map>
#include <string>
#include <vector>

namespace chan::platform {

class SportsScreen {
public:
    void init(lv_obj_t* root);
    void render(const std::vector<Game>& games, const std::map<std::string, Game>& last_results,
                const std::vector<Team>& teams, uint64_t now_epoch);
    void show_banner(const std::string& line, uint32_t color, uint32_t alt_color);
    void hide_banner();

private:
    lv_obj_t* _list = nullptr;
    lv_obj_t* _empty = nullptr;
    lv_obj_t* _subtitle = nullptr;
    lv_obj_t* _banner = nullptr;
    lv_obj_t* _banner_label = nullptr;
    void add_row(const Game& g, const std::vector<Team>& teams, bool final_mode, uint64_t now_epoch);
};

}  // namespace chan::platform
