#pragma once

#include <cstddef>
#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace chan {

// Standard base64 with '=' padding.
std::string b64_encode(const uint8_t* data, size_t len);

// Decodes standard base64 (padding optional). Returns false on an invalid character.
bool b64_decode(std::string_view text, std::vector<uint8_t>& out);

}  // namespace chan
