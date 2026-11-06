/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include "common.h"
#include <smooth_lvgl.hpp>
#include <uitk/short_namespace.hpp>
#include <hal/hal.h>
#include <cstdint>
#include <functional>
#include <memory>
#include <string_view>

namespace setup_workers {

/**
 * @brief
 *
 */
class WorkerBase {
public:
    virtual ~WorkerBase() = default;

    virtual void update()
    {
    }

    bool isDone() const
    {
        return _is_done;
    }

protected:
    bool _is_done = false;
};


/**
 * @brief
 *
 */
class ZeroCalibrationWorker : public WorkerBase {
public:
    ZeroCalibrationWorker();
    void update() override;

private:
    std::unique_ptr<WorkerBase> _page_tips;
    std::unique_ptr<WorkerBase> _page_calibration;
};

/**
 * @brief
 *
 */
class ServoTestWorker : public WorkerBase {
public:
    ServoTestWorker();
    void update() override;

private:
    std::unique_ptr<WorkerBase> _page_tips;
    std::unique_ptr<WorkerBase> _page_test;
    std::unique_ptr<WorkerBase> _page_done;
};


/**
 * @brief
 *
 */
class RgbTestWorker : public WorkerBase {
public:
    RgbTestWorker();
    ~RgbTestWorker();

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::vector<std::unique_ptr<uitk::lvgl_cpp::Button>> _buttons;
};

/**
 * @brief
 *
 */
class MicTestWorker : public WorkerBase {
public:
    MicTestWorker();
    ~MicTestWorker();
    void update() override;

private:
    void update_button_text();
    void update_button_state();
    void update_button_color();
    void update_waveform();

    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Chart> _chart_waveform;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_test;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_back;

    MicTestStatus _status        = MicTestStatus::Done;
    bool _test_flag              = false;
    bool _back_flag              = false;
    bool _is_testing             = false;
    int _waveform_series         = -1;
    uint8_t _original_volume     = 80;
    uint32_t _last_waveform_tick = 0;
    std::string _error_message;
    std::vector<int16_t> _waveform_frame;
};


/**
 * @brief
 *
 */
class WifiStatusWorker : public WorkerBase {
public:
    WifiStatusWorker();
    ~WifiStatusWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_back;
    uint32_t _last_refresh = 0;
    bool _back_clicked     = false;
};

class FwVersionWorker : public WorkerBase {
public:
    FwVersionWorker();
    ~FwVersionWorker();
    void update() override;

private:
    uint32_t _last_tick = 0;
};


/**
 * @brief
 *
 */
class BrightnessSetupWorker : public WorkerBase {
public:
    BrightnessSetupWorker();
    ~BrightnessSetupWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_brightness;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    uint8_t _original_brightness = 0;
    int32_t _target_brightness   = -1;
    bool _confirmed              = false;
};

/**
 * @brief
 *
 */
class VolumeSetupWorker : public WorkerBase {
public:
    VolumeSetupWorker();
    ~VolumeSetupWorker();
    void update() override;

private:
    std::unique_ptr<uitk::lvgl_cpp::Container> _panel;
    std::unique_ptr<uitk::lvgl_cpp::Label> _label_volume;
    std::unique_ptr<uitk::lvgl_cpp::Slider> _slider;
    std::unique_ptr<uitk::lvgl_cpp::Button> _btn_confirm;
    std::vector<uint8_t> _volume_levels;
    uint8_t _original_volume = 0;
    int32_t _target_volume   = -1;
    bool _confirmed          = false;
};






}  // namespace setup_workers
