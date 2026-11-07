# Architecture

Chan is a fork of the official M5Stack Stack-chan firmware (`firmware/`, MIT) with the cloud
parts removed and a companion layer added on top. Read the design in
[`docs/superpowers/specs/2026-11-06-chan-design.md`](superpowers/specs/2026-11-06-chan-design.md).

```
firmware/
├── main/
│   ├── main.cpp                 boot: config → HAL → Chan → avatar/voice runtime (or Settings on warm reboot)
│   ├── chan/core/               pure C++17, no ESP-IDF, host-tested with doctest (firmware/tests)
│   │   ├── config               .env + config.json blob → ChanConfig
│   │   ├── attention            signals → LED pattern / expression / sound, priorities, quiet hours
│   │   ├── gemini_messages      Live API setup/audio/image/tool JSON, server message parser
│   │   ├── tools                tool registry for Gemini function calling
│   │   ├── agents               agent events, store, alerts, re-nudge
│   │   ├── weather              ipwho / Open-Meteo parsing, WMO → icon, IANA → POSIX TZ
│   │   └── sports               ESPN/MLB parsing, score-change detector, last results
│   ├── chan/platform/           thin ESP-IDF adapters
│   │   ├── chan_app             the singleton that owns everything; ticks from the avatar update task
│   │   ├── gemini_protocol      xiaozhi Protocol subclass over a TLS WebSocket to Gemini Live
│   │   ├── http_server          LAN API (/api/health, /api/agents, /api/ota) + mDNS
│   │   ├── http_fetch           HTTP GET → ArduinoJson (PSRAM allocator, filters)
│   │   ├── leds / expressions / sounds / camera_tool / debug_console
│   │   └── screens/             shell (swipes, long-press), face overlay, agents, sports, weather
│   ├── hal/, stackchan/, apps/  stock Stack-chan: drivers, avatar + motion engine, trimmed Settings app
│   └── assets/                  sound clips (synthesized by scripts/make_sfx.py), icons, fonts
├── patches/xiaozhi-esp32.patch  our changes to the pinned xiaozhi voice runtime
└── repos.json / fetch_repos.py  pinned upstream dependencies
```

## Runtime

- **xiaozhi-esp32 v2.5.2** provides mic/speaker, the AFE (echo cancellation + VAD + the
  "Hi Stack-chan" wake word), the LVGL display and a state machine
  (idle → connecting → listening ↔ speaking). Chan keeps it, but replaces the cloud protocol
  with `GeminiProtocol` and removes activation/OTA-check/account code.
- **Audio wire.** Opus is bypassed: the mic's 16 kHz PCM goes straight into
  `realtimeInput.audio`, and Gemini's 24 kHz PCM is queued raw to the playback path (patch in
  `audio_service.cc`, flag `AudioWire::Pcm16`). Device-side AEC keeps the mic open while it speaks,
  so you can interrupt it; `interrupted` from the server flushes local playback.
- **ChanApp** runs inside the stock avatar update task (~50 Hz, LVGL lock held): attention →
  LEDs/face/sounds, screens, agent re-nudges, clock. A separate `chan_net` task (stack in PSRAM)
  does all fetching; it never touches NVS (the timezone write is handed to the update task).
- **Attention** is the single arbiter of LEDs, expression and sounds. Priority:
  Celebrate/Sulk (timed) > Agent alert > Listening > Speaking > Thinking > Offline > idle. Quiet hours
  drop sounds and voice wake and dim LEDs. A head pat or a voice turn acknowledges alerts.
- **Screens.** Face (home, with idle clock + weather), Agents, Sports, Weather; horizontal swipe
  switches, tap = talk, a 2 s long-press opens the stock Settings app (warm reboot) which reboots back
  on close. Head-panel swipes are ignored (the sensor emits phantom gestures); a pat = press held
  400 ms–3 s.

## Memory notes (ESP32-S3, 8 MB PSRAM, ~16–30 KB internal free at run time)

- Internal RAM is the scarce resource once the AFE is up. TLS decryption needs internal bounce
  buffers, so **public data feeds (ESPN, MLB, ipwho.is, Open-Meteo) are fetched over plain
  HTTP** — TLS is reserved for Gemini. The data is public; the trade-off is that someone on your
  Wi-Fi could feed fake scores or weather.
- Feed JSON is parsed with ArduinoJson filters into PSRAM-backed documents (`psram_allocator()`),
  never re-serialised.
- Task stacks that only do I/O live in PSRAM (`chan_net`, mDNS). Tasks that write NVS/flash must
  not (the LVGL task included — hence the deferred Settings reboot).

## Config partition

`scripts/chan.py pack-config` writes `CHAN | u32 len | u32 crc32 | {"env":{…},"config":{…}}` to
the `chancfg` partition (0xE10000, 64 KB). `chan::platform::load_chan_config` validates and parses it
at boot; `chan::parse_config` applies defaults and reports missing required keys.

## Agent hooks

`hooks/chan_notify.py` maps Claude Code, Codex, Gemini CLI and Copilot CLI hook payloads to
`POST /api/agents` (bearer token, ≤ 4 KB, 2 s timeout, always exits 0). `AgentStore` keeps one row per
agent session, alerts once per `needs_attention`, re-nudges once, ages rows out. See
[hooks/README.md](../hooks/README.md).

## OTA

`python3 scripts/chan.py ota` POSTs `build/stack-chan.bin` to `/api/ota` (bearer token; 409 while
a voice session is open). The image goes into the inactive OTA slot, is validated, and the robot
reboots. With rollback enabled the new image is only "pending verify" until `ChanApp` confirms it —
30 s after boot with the UI up, Wi-Fi connected and the config loaded — so a broken image that never
gets that far is rolled back by the bootloader on the next reset. To verify an update really stuck:
push it, wait a minute, power-cycle, and check the boot log still says the new `firmware v…`.

## Testing

- `cmake -S firmware/tests -B build-host && cmake --build build-host && ctest --test-dir build-host`
  — core logic against fixtures captured from the real APIs (`firmware/tests/fixtures`).
- `python3 -m pytest scripts hooks` — packing, OTA client, hook mapping.
- On the robot: the serial debug console (`help`, `attn …`, `fake-score …`, `agent …`, `screen …`,
  `talk`, `look`, `weather`, `overlay`).
