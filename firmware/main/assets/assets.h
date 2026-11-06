/*
 * SPDX-FileCopyrightText: 2026 M5Stack Technology CO LTD
 *
 * SPDX-License-Identifier: MIT
 */
#pragma once
#include <lvgl.h>
#include <string_view>

LV_FONT_DECLARE(MontserratSemiBold26);

extern const char ogg_camera_shutter_start[] asm("_binary_camera_shutter_ogg_start");
extern const char ogg_camera_shutter_end[] asm("_binary_camera_shutter_ogg_end");
static const std::string_view OGG_CAMERA_SHUTTER{
    static_cast<const char*>(ogg_camera_shutter_start),
    static_cast<size_t>(ogg_camera_shutter_end - ogg_camera_shutter_start)};

extern const char ogg_new_notification_start[] asm("_binary_new_notification_ogg_start");
extern const char ogg_new_notification_end[] asm("_binary_new_notification_ogg_end");
static const std::string_view OGG_NEW_NOTIFICATION{
    static_cast<const char*>(ogg_new_notification_start),
    static_cast<size_t>(ogg_new_notification_end - ogg_new_notification_start)};

extern const char ogg_chime_start[] asm("_binary_chime_ogg_start");
extern const char ogg_chime_end[] asm("_binary_chime_ogg_end");
static const std::string_view OGG_CHIME{static_cast<const char*>(ogg_chime_start),
                                     static_cast<size_t>(ogg_chime_end - ogg_chime_start)};

extern const char ogg_nudge_start[] asm("_binary_nudge_ogg_start");
extern const char ogg_nudge_end[] asm("_binary_nudge_ogg_end");
static const std::string_view OGG_NUDGE{static_cast<const char*>(ogg_nudge_start),
                                     static_cast<size_t>(ogg_nudge_end - ogg_nudge_start)};

extern const char ogg_cheer_start[] asm("_binary_cheer_ogg_start");
extern const char ogg_cheer_end[] asm("_binary_cheer_ogg_end");
static const std::string_view OGG_CHEER{static_cast<const char*>(ogg_cheer_start),
                                     static_cast<size_t>(ogg_cheer_end - ogg_cheer_start)};

extern const char ogg_goal_horn_start[] asm("_binary_goal_horn_ogg_start");
extern const char ogg_goal_horn_end[] asm("_binary_goal_horn_ogg_end");
static const std::string_view OGG_GOAL_HORN{static_cast<const char*>(ogg_goal_horn_start),
                                     static_cast<size_t>(ogg_goal_horn_end - ogg_goal_horn_start)};

extern const char ogg_aww_start[] asm("_binary_aww_ogg_start");
extern const char ogg_aww_end[] asm("_binary_aww_ogg_end");
static const std::string_view OGG_AWW{static_cast<const char*>(ogg_aww_start),
                                     static_cast<size_t>(ogg_aww_end - ogg_aww_start)};

namespace assets {

lv_image_dsc_t get_image(std::string_view name);

}  // namespace assets
