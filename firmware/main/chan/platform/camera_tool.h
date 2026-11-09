// One-shot camera capture for the `look` tool: grabs a frame from the stock camera and
// encodes it as JPEG (≤ 640×480, the CoreS3 sensor's size).
#pragma once

#include <cstdint>
#include <string>
#include <vector>

namespace chan::platform {

// Blocks for the capture (~100–300 ms). Returns false with `error` set when the camera
// is missing, busy, or the frame could not be encoded.
bool camera_capture_jpeg(std::vector<uint8_t>& jpeg, std::string& error, int quality = 70);

}  // namespace chan::platform
