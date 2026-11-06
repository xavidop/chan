/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 * SPDX-FileCopyrightText: 2026 Xavier Portilla Edo
 *
 * SPDX-License-Identifier: MIT
 */
#include <smooth_ui_toolkit.hpp>
#include <uitk/short_namespace.hpp>
#include <mooncake_log.h>
#include <mooncake.h>
#include <apps/apps.h>
#include <hal/hal.h>
#include <chan/platform/chan_app.h>
#include <chan/platform/config_store.h>

using namespace mooncake;
using namespace smooth_ui_toolkit;

extern "C" void app_main(void)
{
    // Setup logger
    mclog::set_level(mclog::level_info);
    mclog::set_time_format(mclog::time_format_unix_milliseconds);

    // Chan config (from the chancfg partition; secrets are never logged)
    chan::ChanConfig cfg;
    std::string cfg_err;
    mclog::tagInfo("Chan", "firmware v{}", FIRMWARE_VERSION);
    const bool cfg_ok = chan::platform::load_chan_config(cfg, cfg_err);
    if (cfg_ok) {
        mclog::tagInfo("Chan", "config ok: {}, {} teams", cfg.name, cfg.teams.size());
    } else {
        mclog::tagError("Chan", "config error: {}", cfg_err);
    }

    // HAL init
    GetHAL().init();

    // Setup ui hal
    ui_hal::on_delay([](uint32_t ms) { GetHAL().delay(ms); });
    ui_hal::on_get_tick([]() { return GetHAL().millis(); });

    if (cfg_ok) {
        chan::platform::ChanApp::get().init(cfg);
    }

    // Settings is the only mooncake app left; it is reached by a warm reboot from the
    // companion and reboots back into it when closed (see AppSetup::onClose).
    if (GetHAL().getWarmRebootTarget() == chan::platform::kSettingsWarmBootTarget) {
        GetHAL().clearWarmRebootRequest();
        const int setup_id = GetMooncake().installApp(std::make_unique<AppSetup>());
        GetMooncake().openApp(setup_id);
        while (1) {
            GetHAL().feedTheDog();
            GetHAL().updateHeapStatusLog();
            GetMooncake().update();
        }
    }

    // Companion mode: avatar + voice runtime, with Chan's shell on top. Never returns.
    GetHAL().startXiaozhi();
}
