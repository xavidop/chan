/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <stackchan/stackchan.h>
#include <apps/common/toast/toast.h>
#include <mooncake_log.h>
#include <assets/assets.h>
#include <hal/hal.h>
#include <vector>
#include <string>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

static std::string _tag = "Setup-System";

struct TimezoneOption_t {
    std::string name;
    std::string tz_posix;
};

static const std::vector<TimezoneOption_t> _timezone_list = {
    {"Baker Is. (UTC-12)", "BIT12"},  {"Midway Island (UTC-11)", "SST11"}, {"Honolulu (UTC-10)", "HST10"},
    {"Alaska (UTC-9)", "AKST9"},      {"Los Angeles (UTC-8)", "PST8"},     {"Denver (UTC-7)", "MST7"},
    {"Chicago (UTC-6)", "CST6"},      {"New York (UTC-5)", "EST5"},        {"Halifax (UTC-4)", "AST4"},
    {"S.Paulo (UTC-3)", "BRT3"},      {"S.Georgia (UTC-2)", "GST2"},       {"Azores (UTC-1)", "AZOT1"},
    {"London (UTC+0)", "GMT0"},       {"Berlin (UTC+1)", "CET-1"},         {"Cairo (UTC+2)", "EET-2"},
    {"Moscow (UTC+3)", "MSK-3"},      {"Dubai (UTC+4)", "GST-4"},          {"Karachi (UTC+5)", "PKT-5"},
    {"Dhaka (UTC+6)", "BST-6"},       {"Bangkok (UTC+7)", "ICT-7"},        {"Beijing (UTC+8)", "CST-8"},
    {"Tokyo (UTC+9)", "JST-9"},       {"Sydney (UTC+10)", "AEST-10"},      {"Noumea (UTC+11)", "SBT-11"},
    {"Auckland (UTC+12)", "NZST-12"}, {"Fiji (UTC+13)", "FJT-13"},         {"Line Islands (UTC+14)", "LINT-14"}};

VolumeSetupWorker::VolumeSetupWorker()
{
    mclog::info("VolumeSetupWorker start");

    for (int volume = 0; volume <= 100; volume += 5) {
        _volume_levels.push_back(volume);
    }

    uint8_t current_volume = GetHAL().getSpeakerVolume();
    _original_volume       = current_volume;
    int current_index      = _volume_levels.size() - 1;
    for (size_t i = 0; i < _volume_levels.size(); i++) {
        if (_volume_levels[i] >= current_volume) {
            current_index = static_cast<int>(i);
            break;
        }
    }

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label_volume = std::make_unique<Label>(*_panel);
    _label_volume->setText(fmt::format("{}%", _volume_levels[current_index]));
    _label_volume->setTextFont(&lv_font_montserrat_24);
    _label_volume->setTextColor(lv_color_hex(0x26206A));
    _label_volume->align(LV_ALIGN_CENTER, 0, -70);

    _slider = std::make_unique<Slider>(*_panel);
    _slider->align(LV_ALIGN_CENTER, 0, -12);
    _slider->setRange(0, _volume_levels.size() - 1);
    _slider->setSize(250, 18);
    _slider->setBgColor(lv_color_hex(0x615B9E), LV_PART_KNOB);
    _slider->setBgColor(lv_color_hex(0x615B9E), LV_PART_INDICATOR);
    _slider->setBgColor(lv_color_hex(0xB8D3FD), LV_PART_MAIN);
    _slider->setBgOpa(255);
    _slider->setValue(current_index);
    _slider->onValueChanged().connect([this](int32_t value) {
        _label_volume->setText(fmt::format("{}%", _volume_levels[value]));
        _target_volume = _volume_levels[value];
    });

    _btn_confirm = std::make_unique<Button>(*_panel);
    apply_button_common_style(*_btn_confirm);
    _btn_confirm->align(LV_ALIGN_CENTER, 0, 60);
    _btn_confirm->setSize(150, 50);
    _btn_confirm->label().setText("Confirm");
    _btn_confirm->onClick().connect([this]() {
        _confirmed = true;
        _is_done   = true;
    });
}

VolumeSetupWorker::~VolumeSetupWorker()
{
    if (_confirmed) {
        auto volume = _volume_levels[_slider->getValue()];
        mclog::tagInfo(_tag, "final volume: {}", volume);
        GetHAL().setSpeakerVolume(volume, true);
        return;
    }

    mclog::tagInfo(_tag, "volume change cancelled, restore: {}", _original_volume);
    GetHAL().setSpeakerVolume(_original_volume, false);
}

void VolumeSetupWorker::update()
{
    if (_target_volume != -1) {
        GetHAL().setSpeakerVolume(_target_volume, false);
        _target_volume = -1;
    }
}


