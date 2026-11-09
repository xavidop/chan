#include "config_store.h"

#include <esp_partition.h>
#include <esp_rom_crc.h>

#include <cstring>
#include <vector>

namespace chan::platform {

namespace {

constexpr const char* kPartitionLabel = "chancfg";
constexpr auto kPartitionSubtype = static_cast<esp_partition_subtype_t>(0x40);
constexpr char kMagic[4] = {'C', 'H', 'A', 'N'};
constexpr size_t kHeaderSize = 12;  // magic(4) + length(4) + crc(4), little endian

uint32_t read_le32(const uint8_t* p)
{
    return static_cast<uint32_t>(p[0]) | (static_cast<uint32_t>(p[1]) << 8) | (static_cast<uint32_t>(p[2]) << 16) |
           (static_cast<uint32_t>(p[3]) << 24);
}

}  // namespace

bool load_chan_config(ChanConfig& out, std::string& error)
{
    const esp_partition_t* part =
        esp_partition_find_first(ESP_PARTITION_TYPE_DATA, kPartitionSubtype, kPartitionLabel);
    if (!part) {
        error = "chancfg partition not found; flash a firmware with the Chan partition table";
        return false;
    }

    uint8_t header[kHeaderSize];
    if (esp_partition_read(part, 0, header, kHeaderSize) != ESP_OK) {
        error = "failed to read chancfg header";
        return false;
    }
    if (std::memcmp(header, kMagic, sizeof(kMagic)) != 0) {
        error = "no config on device; run `python3 scripts/chan.py flash-config`";
        return false;
    }
    const uint32_t length = read_le32(header + 4);
    const uint32_t expected_crc = read_le32(header + 8);
    if (length == 0 || length > part->size - kHeaderSize) {
        error = "chancfg length out of range: " + std::to_string(length);
        return false;
    }

    std::vector<uint8_t> payload(length);
    if (esp_partition_read(part, kHeaderSize, payload.data(), length) != ESP_OK) {
        error = "failed to read chancfg payload";
        return false;
    }
    const uint32_t actual_crc = esp_rom_crc32_le(0, payload.data(), length);
    if (actual_crc != expected_crc) {
        error = "chancfg CRC mismatch; re-run `python3 scripts/chan.py flash-config`";
        return false;
    }

    return parse_config(std::string_view(reinterpret_cast<const char*>(payload.data()), length), out, error);
}

}  // namespace chan::platform
