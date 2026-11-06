#include "agents_screen.h"

#include <cstdio>

namespace chan::platform {

namespace {
constexpr uint32_t kFg = 0xF2F5F7;
constexpr uint32_t kMuted = 0x8A94A6;
constexpr uint32_t kRowBg = 0x1A2030;
constexpr uint32_t kRowAlertBg = 0x3A2A1C;
constexpr uint32_t kFallbackColor = 0xFFA000;

std::string ago_text(uint32_t ms)
{
    const uint32_t s = ms / 1000;
    char buf[16];
    if (s < 60) snprintf(buf, sizeof(buf), "%us", static_cast<unsigned>(s));
    else if (s < 3600) snprintf(buf, sizeof(buf), "%um", static_cast<unsigned>(s / 60));
    else snprintf(buf, sizeof(buf), "%uh", static_cast<unsigned>(s / 3600));
    return buf;
}

const char* state_label(AgentState s, bool acknowledged)
{
    switch (s) {
        case AgentState::NeedsAttention: return acknowledged ? "waiting" : "NEEDS YOU";
        case AgentState::Done: return "done";
        case AgentState::Working: return "working";
    }
    return "";
}

std::string pretty_agent(const std::string& agent)
{
    if (agent == "claude-code") return "Claude Code";
    if (agent == "codex") return "Codex";
    if (agent == "gemini") return "Gemini CLI";
    if (agent == "copilot") return "Copilot";
    return agent;
}
}  // namespace

void AgentsScreen::init(lv_obj_t* root)
{
    // Hide the shell's placeholder hint (second label) once we own the body.
    if (lv_obj_get_child_count(root) >= 2) _empty = lv_obj_get_child(root, 1);

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
}

void AgentsScreen::render(const std::vector<AgentRow>& rows, uint32_t now_ms, const std::map<std::string, uint32_t>& colors)
{
    if (!_list) return;
    lv_obj_clean(_list);
    if (_empty) {
        if (rows.empty()) lv_obj_remove_flag(_empty, LV_OBJ_FLAG_HIDDEN);
        else lv_obj_add_flag(_empty, LV_OBJ_FLAG_HIDDEN);
    }
    for (const auto& r : rows) {
        const bool alert = r.state == AgentState::NeedsAttention && !r.acknowledged;
        auto it = colors.find(r.agent);
        const uint32_t color = it != colors.end() ? it->second : kFallbackColor;

        lv_obj_t* row = lv_obj_create(_list);
        lv_obj_set_size(row, 320 - 16, 44);
        lv_obj_set_style_bg_color(row, lv_color_hex(alert ? kRowAlertBg : kRowBg), 0);
        lv_obj_set_style_bg_opa(row, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(row, alert ? 2 : 0, 0);
        lv_obj_set_style_border_color(row, lv_color_hex(color), 0);
        lv_obj_set_style_radius(row, 10, 0);
        lv_obj_set_style_pad_all(row, 6, 0);
        lv_obj_remove_flag(row, LV_OBJ_FLAG_SCROLLABLE);
        lv_obj_add_flag(row, LV_OBJ_FLAG_GESTURE_BUBBLE);

        lv_obj_t* dot = lv_obj_create(row);
        lv_obj_set_size(dot, 12, 12);
        lv_obj_set_style_radius(dot, LV_RADIUS_CIRCLE, 0);
        lv_obj_set_style_bg_color(dot, lv_color_hex(color), 0);
        lv_obj_set_style_bg_opa(dot, LV_OPA_COVER, 0);
        lv_obj_set_style_border_width(dot, 0, 0);
        lv_obj_align(dot, LV_ALIGN_LEFT_MID, 2, 0);

        lv_obj_t* name = lv_label_create(row);
        lv_label_set_text_fmt(name, "%s  %s", pretty_agent(r.agent).c_str(), r.project.c_str());
        lv_obj_set_style_text_font(name, &lv_font_montserrat_16, 0);
        lv_obj_set_style_text_color(name, lv_color_hex(kFg), 0);
        lv_obj_set_width(name, 200);
        lv_label_set_long_mode(name, LV_LABEL_LONG_DOT);
        lv_obj_align(name, LV_ALIGN_TOP_LEFT, 22, -2);

        lv_obj_t* state = lv_label_create(row);
        lv_label_set_text(state, state_label(r.state, r.acknowledged));
        lv_obj_set_style_text_font(state, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(state, lv_color_hex(alert ? color : kMuted), 0);
        lv_obj_align(state, LV_ALIGN_BOTTOM_LEFT, 22, 2);

        lv_obj_t* ago = lv_label_create(row);
        lv_label_set_text(ago, ago_text(now_ms - r.since_ms).c_str());
        lv_obj_set_style_text_font(ago, &lv_font_montserrat_14, 0);
        lv_obj_set_style_text_color(ago, lv_color_hex(kMuted), 0);
        lv_obj_align(ago, LV_ALIGN_RIGHT_MID, -4, 0);
    }
}

}  // namespace chan::platform
