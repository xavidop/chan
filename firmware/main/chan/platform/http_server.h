// Chan's small LAN HTTP API, advertised over mDNS as <hostname>.local:
//   GET  /api/health              → {"ok":true,"name":"Chan"}            (no auth)
//   POST /api/agents              → 204; body ≤ 4096 bytes; Authorization: Bearer <CHAN_AGENT_TOKEN>
// Later: POST /api/ota.
#pragma once

#include <chan/core/config.h>

namespace chan::platform {

// Starts the server and mDNS once (idempotent). Call after Wi-Fi is connected.
void http_server_start(const ChanConfig& cfg);
bool http_server_running();

}  // namespace chan::platform
