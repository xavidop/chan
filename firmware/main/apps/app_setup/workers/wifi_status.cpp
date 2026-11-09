/*
 * SPDX-FileCopyrightText: 2026 Xavier Portilla Edo
 *
 * SPDX-License-Identifier: MIT
 */
#include "workers.h"
#include <mooncake_log.h>
#include <hal/hal.h>
#include <wifi_manager.h>

using namespace smooth_ui_toolkit::lvgl_cpp;
using namespace setup_workers;

WifiStatusWorker::WifiStatusWorker()
{
    mclog::info("WifiStatusWorker start");

    _panel = std::make_unique<Container>(lv_screen_active());
    _panel->setBgColor(lv_color_hex(0xEDF4FF));
    _panel->align(LV_ALIGN_CENTER, 0, 0);
    _panel->setBorderWidth(0);
    _panel->setSize(320, 240);
    _panel->setRadius(0);
    _panel->removeFlag(LV_OBJ_FLAG_SCROLLABLE);

    _label = std::make_unique<Label>(*_panel);
    _label->setTextFont(&lv_font_montserrat_16);
    _label->setTextColor(lv_color_hex(0x26206A));
    _label->align(LV_ALIGN_CENTER, 0, -30);
    _label->setText("Checking…");

    _btn_back = std::make_unique<Button>(*_panel);
    apply_button_common_style(*_btn_back);
    _btn_back->align(LV_ALIGN_CENTER, 0, 70);
    _btn_back->setSize(150, 50);
    _btn_back->label().setText("Back");
    _btn_back->onClick().connect([this]() { _back_clicked = true; });
}

WifiStatusWorker::~WifiStatusWorker()
{
}

void WifiStatusWorker::update()
{
    if (_back_clicked) {
        _is_done = true;
        return;
    }
    const uint32_t now = GetHAL().millis();
    if (now - _last_refresh < 1000) return;
    _last_refresh = now;

    auto& wifi = WifiManager::GetInstance();
    if (wifi.IsConnected()) {
        _label->setText(fmt::format("Connected to {}\nIP {}\nSignal {} dBm", wifi.GetSsid(), wifi.GetIpAddress(), wifi.GetRssi()));
    } else {
        _label->setText("Not connected\n\nEdit WIFI_SSID / WIFI_PASSWORD in .env\nand run: chan.py flash-config");
    }
    _label->align(LV_ALIGN_CENTER, 0, -30);
}
