// Reads the Chan config blob from the `chancfg` flash partition (see firmware/partitions.csv
// and scripts/chan.py pack-config for the on-flash layout).
#pragma once

#include <chan/core/config.h>

#include <string>

namespace chan::platform {

// Loads and validates the partition (magic, length, CRC) and parses it into `out`.
// On failure returns false with a human-readable `error`; never logs secret values.
bool load_chan_config(ChanConfig& out, std::string& error);

}  // namespace chan::platform
