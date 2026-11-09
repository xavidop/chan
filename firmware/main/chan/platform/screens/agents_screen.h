// The Agents overlay: one row per coding-agent session. Call with the LVGL lock held.
#pragma once

#include <chan/core/agents.h>

#include <lvgl.h>

#include <map>
#include <string>
#include <vector>

namespace chan::platform {

class AgentsScreen {
public:
    // `root` is the Shell's agents overlay (title already drawn at the top).
    void init(lv_obj_t* root);
    void render(const std::vector<AgentRow>& rows, uint32_t now_ms, const std::map<std::string, uint32_t>& colors);

private:
    lv_obj_t* _list = nullptr;
    lv_obj_t* _empty = nullptr;
};

}  // namespace chan::platform
