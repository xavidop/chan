#include "debug_console.h"

#include <driver/usb_serial_jtag.h>
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>
#include <mooncake_log.h>

#include <string>

namespace chan::platform {

namespace {

constexpr const char* kTag = "DebugConsole";
std::function<void(std::string_view)> g_handler;

void reader_task(void*)
{
    std::string line;
    uint8_t buf[64];
    while (true) {
        int n = usb_serial_jtag_read_bytes(buf, sizeof(buf), pdMS_TO_TICKS(200));
        for (int i = 0; i < n; ++i) {
            char c = static_cast<char>(buf[i]);
            if (c == '\r' || c == '\n') {
                while (!line.empty() && (line.back() == ' ' || line.back() == '\t')) line.pop_back();
                if (!line.empty() && g_handler) g_handler(line);
                line.clear();
            } else if (c >= 32 && line.size() < 200) {
                line.push_back(c);
            }
        }
    }
}

}  // namespace

void debug_console_start(std::function<void(std::string_view)> handler)
{
    g_handler = std::move(handler);
    usb_serial_jtag_driver_config_t cfg = USB_SERIAL_JTAG_DRIVER_CONFIG_DEFAULT();
    esp_err_t err = usb_serial_jtag_driver_install(&cfg);
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        mclog::tagWarn(kTag, "usb serial jtag driver install failed: {}", static_cast<int>(err));
        return;
    }
    xTaskCreate(reader_task, "chan_dbg", 4096, nullptr, 1, nullptr);
    mclog::tagInfo(kTag, "ready; type `help`");
}

}  // namespace chan::platform
