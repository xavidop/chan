#include "camera_tool.h"

#include <hal/board/hal_bridge.h>
#include <hal/board/stackchan_camera.h>
#include <jpg/image_to_jpeg.h>
#include <mooncake_log.h>

#include <cstdlib>

namespace chan::platform {

namespace {
constexpr const char* kTag = "Camera";
}

bool camera_capture_jpeg(std::vector<uint8_t>& jpeg, std::string& error, int quality)
{
    StackChanCamera* camera = hal_bridge::board_get_camera();
    if (!camera) {
        error = "no camera on this board";
        return false;
    }
    if (!camera->Capture()) {
        error = "camera capture failed";
        return false;
    }
    auto lock = camera->TryLockFrame();
    if (!lock.owns_lock()) {
        error = "camera busy";
        return false;
    }
    const uint8_t* data = camera->GetFrameData();
    const size_t len = camera->GetFrameSize();
    const int w = camera->GetFrameWidth();
    const int h = camera->GetFrameHeight();
    if (!data || len == 0 || w <= 0 || h <= 0) {
        error = "empty camera frame";
        return false;
    }
    if (camera->GetFrameFormat() == V4L2_PIX_FMT_JPEG) {
        jpeg.assign(data, data + len);
        return true;
    }
    uint8_t* out = nullptr;
    size_t out_len = 0;
    if (!image_to_jpeg(const_cast<uint8_t*>(data), len, static_cast<uint16_t>(w), static_cast<uint16_t>(h),
                       camera->GetFrameFormat(), static_cast<uint8_t>(quality), &out, &out_len) ||
        !out) {
        error = "jpeg encoding failed";
        return false;
    }
    jpeg.assign(out, out + out_len);
    free(out);
    mclog::tagInfo(kTag, "captured {}x{} -> {} bytes jpeg", w, h, out_len);
    return true;
}

}  // namespace chan::platform
