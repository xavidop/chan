# Chan Firmware Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Replace the official M5Stack Stack-chan firmware with Chan: a cute companion with Gemini Live voice (web search + camera), coding-agent alerts, live sports celebrations, and an idle clock/weather.

**Architecture:** Fork `m5stack/StackChan/firmware` (ESP-IDF 6.1 + xiaozhi-esp32 v2.5.2 + mooncake + smooth_ui_toolkit). Chan always boots straight into the stock "xiaozhi mode" (avatar + audio engine running) and draws its own screens as LVGL overlays over the avatar; the stock mooncake launcher is only used for the Settings screen via the stock warm-reboot mechanism. All parsing/decision logic lives in `firmware/main/chan/core/` (pure C++17, no ESP-IDF includes) and is unit-tested on the host; `firmware/main/chan/platform/` holds thin ESP-IDF adapters (HTTP, Wi-Fi, LEDs, camera, protocol).

**Tech Stack:** ESP-IDF 6.1, C++17, LVGL 9.5, ArduinoJson 7.4.2 (already pinned upstream), doctest 2.4.11 (host tests, vendored single header), Python 3 stdlib + pytest (scripts and hooks), GitHub Actions.

**Spec:** `docs/superpowers/specs/2026-11-06-chan-design.md`

## Global Constraints

- Hardware: M5Stack Stack-chan (CoreS3, ESP32-S3, 16 MB flash, 8 MB PSRAM, 320×240). Board type `CONFIG_BOARD_TYPE_M5STACK_STACK_CHAN=y`.
- Toolchain: ESP-IDF `>=6.1.0,<6.2.0`. Upstream deps stay pinned via `firmware/repos.json` + `firmware/fetch_repos.py`; Chan's changes to xiaozhi go in `firmware/patches/xiaozhi-esp32.patch` (regenerated, never hand-edited in `xiaozhi-esp32/`).
- Wake word: `CONFIG_SR_WN_WN9L_HISTACKCHAN_TTS3=y` ("Hi Stack-chan").
- Secrets: only in `.env` (gitignored). `config.json` gitignored. Commit only `.env.example` / `config.example.json`. Never log a secret value (log key names only). `backup/` stays gitignored.
- Chan never speaks unprompted. Only four attention triggers: wake word, screen tap, agent needs attention, followed-team scoring event. Celebrations/nudges use bundled sound clips, never a Gemini session.
- Quiet hours default `23:00`–`07:00`: no sound, no voice wake, LEDs dimmed; screens still update.
- Agent HTTP API requires `Authorization: Bearer <CHAN_AGENT_TOKEN>`; mDNS hostname default `chan`.
- Sports: soccer (ESPN), NFL, NBA, NHL (ESPN), MLB (`statsapi.mlb.com`). Weather: `ipwho.is` + Open-Meteo, no keys.
- Agent colours default: Claude Code `#D97757`, Codex `#10A37F`, Gemini `#4285F4`.
- Credit M5Stack and xiaozhi (keep their SPDX headers; add `NOTICE`). License MIT.
- Every task ends with a firmware that builds, flashes, and boots to a working robot.

## Review Focus

1. **Wi-Fi drops / no internet at boot** → Chan shows the offline state (red LED blink, confused face), keeps last weather/scores, recovers on its own without reboot. Test: `tracker keeps last games when a poll fails` (Task 9), `offline outranks idle but not agent alert` (Task 4).
2. **Duplicate or replayed scoring events** (reconnect, poller restart, ESPN score correction going *down*) → never celebrate twice, never celebrate a decrease. Test: `no event on first observation`, `no event on score decrease`, `same score twice fires once` (Task 9).
3. **Hook storm / robot off** → many agent events in a burst, or the robot is unreachable: hooks return within ~1 s and never fail the agent; device keeps at most `agents.maxSessions` rows. Tests: `notify exits 0 when host unreachable` (Task 7), `store evicts oldest beyond max sessions` (Task 7).
4. **Malformed / oversized input** (bad config.json, missing .env key, garbage POST body, huge ESPN payload) → clear error, safe defaults, no crash. Tests: `missing GEMINI key is reported`, `invalid json returns error` (Task 2), `rejects body over 4096 bytes` (Task 7), `parses real scoreboard fixture with filter` (Task 9).
5. **Quiet hours spanning midnight** and alerts during quiet hours → no sound 23:00–07:00 including the wrap, alert still shown silently. Test: `quiet hours wrap midnight` (Task 4).

---

## File Structure

```
.
├── .env.example                  # secrets & Wi-Fi (documented)
├── config.example.json           # everything else (documented, Dash-style)
├── NOTICE                        # upstream credits
├── scripts/
│   ├── chan.py                   # CLI: setup, build, flash, pack-config, flash-config, ota, restore-stock, monitor
│   └── test_chan.py              # pytest for pack-config
├── hooks/
│   ├── chan_notify.py            # one stdlib-only notifier for claude/codex/gemini/generic
│   ├── test_chan_notify.py
│   └── README.md                 # how to wire each agent
├── docs/                         # spec, plan, setup guides
├── .github/workflows/ci.yml      # host tests, pytest, secret scan, firmware build
└── firmware/                     # forked from m5stack/StackChan/firmware
    ├── repos.json, fetch_repos.py, patches/xiaozhi-esp32.patch, partitions.csv, sdkconfig.defaults
    ├── tests/                    # host tests (CMake + doctest)
    │   ├── CMakeLists.txt
    │   ├── doctest.h
    │   ├── fixtures/             # real JSON samples (ESPN, MLB, ipwho, open-meteo, gemini)
    │   └── *_test.cpp
    └── main/
        ├── main.cpp              # boots ChanApp (or stock AppSetup on warm reboot)
        ├── chan/core/            # pure logic, host-tested
        │   ├── config.{h,cpp}        quiet_hours.{h,cpp}   attention.{h,cpp}
        │   ├── agents.{h,cpp}        sports.{h,cpp}        weather.{h,cpp}
        │   ├── gemini_messages.{h,cpp}  base64.{h,cpp}    tools.{h,cpp}
        └── chan/platform/        # ESP-IDF adapters
            ├── chan_app.{h,cpp}      # owns everything, main loop/timers
            ├── config_store.{h,cpp}  # reads config partition
            ├── leds.{h,cpp}  sounds.{h,cpp}  expressions.{h,cpp}
            ├── http_server.{h,cpp}   # /api/health /api/agents /api/ota + mDNS
            ├── http_fetch.{h,cpp}    # TLS GET with ArduinoJson filter into PSRAM
            ├── gemini_protocol.{h,cpp}  # xiaozhi Protocol subclass
            ├── camera_tool.{h,cpp}
            └── screens/  shell.{h,cpp} face_overlay.cpp agents_screen.cpp sports_screen.cpp
```

Core headers use namespace `chan`; platform uses `chan::platform`.

---

### Task 1: Import upstream firmware, toolchain, stock build in this repo

**Files:**
- Create: `firmware/` (copy of `m5stack/StackChan` `firmware/` at its current `main` commit — record the commit SHA in `NOTICE`), `NOTICE`, `scripts/chan.py` (subcommands `setup`, `build`, `flash`, `monitor`, `restore-stock`), `.github/workflows/ci.yml` (job `firmware-build` using the `espressif/idf:v6.1` container: `python3 fetch_repos.py && idf.py build`)
- Modify: `.gitignore` (add `firmware/build/`, `firmware/managed_components/`, `firmware/xiaozhi-esp32/`, `firmware/components/`, `firmware/sdkconfig`, `firmware/dependencies.lock` stays committed)

**Interfaces:**
- Produces: `python3 scripts/chan.py build|flash|monitor|restore-stock [--port PORT]`. `restore-stock` writes `backup/stackchan-stock-16MB.bin` at offset 0 after verifying `backup/SHA256SUMS`.

- [ ] **Step 1:** Install ESP-IDF v6.1 on the Mac per Espressif's macOS guide (EIM installer or `git clone -b v6.1 --recursive https://github.com/espressif/esp-idf ~/esp/esp-idf && ~/esp/esp-idf/install.sh esp32s3`). `chan.py setup` documents/sources `export.sh`. Verify: `idf.py --version` prints `v6.1`.
- [ ] **Step 2:** Copy upstream `firmware/` in, add `NOTICE`, update `.gitignore`. Verify: `git status` shows no files from `xiaozhi-esp32/`, `components/`, `build/`.
- [ ] **Step 3:** `cd firmware && python3 fetch_repos.py && idf.py set-target esp32s3 && idf.py build`. Expected: `Project build complete`.
- [ ] **Step 4:** Implement `scripts/chan.py` (argparse; shells out to `idf.py`/`esptool`). `restore-stock` refuses if checksum mismatches.
- [ ] **Step 5:** `python3 scripts/chan.py flash --port /dev/cu.usbmodem101`. Expected: robot boots to the stock UI (proves our build of upstream works on this unit).
- [ ] **Step 6:** Add CI workflow; push; CI `firmware-build` green.
- [ ] **Step 7:** Commit `chore: import upstream StackChan firmware and build tooling`.

### Task 2: Host test harness + config (parse, pack, flash, load)

**Files:**
- Create: `firmware/tests/CMakeLists.txt` (add doctest + ArduinoJson include from `firmware/components/ArduinoJson/src`, glob `firmware/main/chan/core/*.cpp`, one executable `chan_core_tests`, keep upstream `motion_math_test`), `firmware/tests/doctest.h`, `firmware/tests/config_test.cpp`, `firmware/main/chan/core/config.{h,cpp}`, `firmware/main/chan/platform/config_store.{h,cpp}`, `.env.example`, `config.example.json`, `scripts/test_chan.py`
- Modify: `firmware/partitions.csv` (add `chancfg, data, 0x40, 0xE10000, 0x10000,` after `coredump`), `scripts/chan.py` (add `pack-config`, `flash-config`), `.github/workflows/ci.yml` (job `host-tests`: cmake/ctest + pytest)

**Config blob format (decided):** `pack-config` reads `.env` + `config.json`, emits one JSON `{"env": {KEY: value…}, "config": {…}}`, written to partition `chancfg` as `magic "CHAN"(4) | u32 length LE | u32 crc32 LE | json bytes`. Unknown `.env` keys are carried through.

**`.env.example` keys:** `WIFI_SSID`, `WIFI_PASSWORD`, `GEMINI_API_KEY`, `CHAN_AGENT_TOKEN`, `OPENAI_API_KEY` (unused v1), `GITHUB_TOKEN` (unused v1).

**`config.example.json` top-level keys:** `name` ("Chan"), `language` ("es"), `timezone` ("auto"), `units` ("metric"), `hostname` ("chan"), `persona` {`prompt`, `chattiness` 0–2}, `gemini` {`model`, `voice`, `sessionIdleSeconds` 20}, `quietHours` {`enabled` true, `start` "23:00", `end` "07:00"}, `volume` 70, `brightness` 70, `screens` {`agents` true, `sports` true, `weather` true}, `agents` {`maxSessions` 8, `staleMinutes` 120, `renudgeMinutes` 5, `colors` {…Global Constraints}}, `teams` [{`key`,`league`,`name`,`displayName`,`color`,`altColor`}] seeded with the owner's Dash teams for leagues in scope (Valencia `esp.1`, Toronto Blue Jays `mlb`, Toronto FC `usa.1`, Toronto Raptors `nba`, Spain `uefa.nations`).

**Interfaces:**
- Produces (`chan/core/config.h`):
  `struct QuietHours { bool enabled; int start_min; int end_min; };`
  `struct Team { std::string key, league, name, display_name; uint32_t color, alt_color; };`
  `struct ChanConfig { std::string wifi_ssid, wifi_password, gemini_api_key, agent_token, name, language, timezone, units, hostname, persona_prompt, gemini_model, gemini_voice; int chattiness, gemini_idle_s, volume, brightness, agents_max_sessions, agents_stale_min, agents_renudge_min; QuietHours quiet; bool screen_agents, screen_sports, screen_weather; std::map<std::string,uint32_t> agent_colors; std::vector<Team> teams; };`
  `bool parse_config(std::string_view blob_json, ChanConfig& out, std::string& error);` — missing optional keys take the example defaults; missing `WIFI_SSID`, `GEMINI_API_KEY` or `CHAN_AGENT_TOKEN` → false with error naming the key.
  `int parse_hhmm(std::string_view)` → minutes or -1. `uint32_t parse_hex_color(std::string_view)`.
- Produces (`platform/config_store.h`): `bool load_chan_config(ChanConfig& out, std::string& error);` reads/validates `chancfg` (magic, length ≤ 0x10000-12, crc).

- [ ] **Step 1: Write failing tests** `config_test.cpp`: `parses full example` (name=="Chan", quiet.start_min==1380, quiet.end_min==420, teams.size()==5, agent_colors["codex"]==0x10A37F); `defaults when optional keys missing` (volume==70, gemini_idle_s==20); `missing GEMINI key is reported` (false, error contains "GEMINI_API_KEY"); `invalid json returns error`; `parse_hhmm rejects 24:00 and 7:5`.
- [ ] **Step 2:** `cmake -S firmware/tests -B build-host && cmake --build build-host && ctest --test-dir build-host --output-on-failure` → FAIL (undefined).
- [ ] **Step 3:** Implement `config.cpp` with ArduinoJson.
- [ ] **Step 4:** Run ctest → PASS.
- [ ] **Step 5: Failing pytest** `test_chan.py`: `test_pack_roundtrip` (header magic `b"CHAN"`, length & crc32 match, JSON has `env.GEMINI_API_KEY`), `test_pack_rejects_missing_env_file`, `test_pack_never_prints_secret_values` (capsys output contains key names, not values).
- [ ] **Step 6:** Implement `pack-config` / `flash-config` (`esptool write-flash 0xE10000 <bin>`); run pytest → PASS.
- [ ] **Step 7:** Implement `load_chan_config`; call it from `main.cpp` at boot and log `config ok: <name>, <n> teams` or the error (no values).
- [ ] **Step 8:** Build, `chan.py flash` + `chan.py flash-config`, monitor. Expected log `config ok: Chan, 5 teams`. Note: re-flashing the partition table moves nothing below `0xE10000`.
- [ ] **Step 9:** Commit `feat: config files, packing and on-device loading`.

### Task 3: Chan shell — strip stock features, boot to face, screens and input

**Files:**
- Modify: `firmware/main/main.cpp` (always `startXiaozhi` path unless warm-reboot target is Settings), `firmware/main/hal/hal.cpp` (remove reminder handler), `firmware/main/hal/hal_mcp.cpp` (drop reminder tools), `firmware/main/CMakeLists.txt`
- Delete: `main/apps/app_app_center`, `app_ezdata`, `app_espnow_ctrl`, `app_dance`, `app_avatar`, `app_ai_agent`, `app_launcher`, `apps/common/reminder`, `hal/hal_ws_avatar.cpp`, `hal/hal_account.cpp`, `hal/hal_ble.cpp`, `hal/hal_espnow.cpp`, `hal/hal_ezdata.cpp`, `hal/hal_app_center.cpp`, `hal/utils/local_network`, `hal/utils/bleprph`, `hal/utils/espnow_radio`, `hal/utils/secret_logic`; prune setup workers `account`, `connectivity` (ESP-NOW/BLE parts), `ai_agent`.
- Create: `chan/platform/chan_app.{h,cpp}`, `chan/platform/screens/shell.{h,cpp}`
- Modify: `firmware/patches/xiaozhi-esp32.patch` — Wi-Fi credentials come from `ChanConfig` (add SSID via esp-wifi-connect's SSID manager before `WifiStation` starts; verify the exact call in `78/esp-wifi-connect` ~3.3), no xiaozhi activation/OTA check on boot.

**Interfaces:**
- Produces: `enum class Screen { Face, Agents, Sports, Settings };`
  `class Shell { public: void show(Screen); void next(); void prev(); Screen current() const; std::function<void()> on_tap; };` — overlays are LVGL containers on `lv_screen_active()` above the avatar (follow `ReminderView` decorator pattern); `next/prev` skip screens disabled in config; `show(Settings)` triggers the stock warm reboot into `AppSetup`; leaving Settings reboots back.
  `class ChanApp { public: static ChanApp& get(); void start(const ChanConfig&); Shell& shell(); const ChanConfig& config() const; };`
- Input (decided): tap on screen → `on_tap` (wired to "start listening" in Task 5); horizontal swipe → `next/prev`; head panel front→back swipe → `next`; head pat → stock head-pet reaction (acknowledge hook added in Task 4).

- [ ] **Step 1:** Delete the listed code; build until it compiles; boot. Expected: robot boots directly to the animated face (blink, breath, idle motion), no launcher, no account/BLE prompts.
- [ ] **Step 2:** Implement `Shell` with placeholder Agents/Sports overlays (title only). Verify on device: swipe left/right cycles Face → Agents → Sports → Face; head swipe advances; Settings opens stock setup and its back/exit returns to Chan.
- [ ] **Step 3:** Trim `AppSetup` to Wi-Fi status, volume, brightness, servo calibration, about. Verify on device.
- [ ] **Step 4:** Regenerate patch: `cd firmware/xiaozhi-esp32 && git diff > ../patches/xiaozhi-esp32.patch`; clean `fetch_repos.py` re-fetch + build passes.
- [ ] **Step 5:** Commit `feat: Chan shell, strip stock apps, boot to face`.

### Task 4: Attention — LEDs, expressions, sounds, quiet hours

**Files:**
- Create: `chan/core/quiet_hours.{h,cpp}`, `chan/core/attention.{h,cpp}`, `tests/attention_test.cpp`, `chan/platform/leds.{h,cpp}`, `chan/platform/expressions.{h,cpp}`, `chan/platform/sounds.{h,cpp}`, `firmware/main/assets/sfx/{cheer,goal_horn,aww,chime,nudge}.ogg` (CC0 sources listed in `NOTICE`; convert with upstream `assets/sfx/convert_cmd.sh`)

**Interfaces:**
- Produces (`core/quiet_hours.h`): `bool in_quiet_hours(const QuietHours&, int minute_of_day);`
- Produces (`core/attention.h`):
  `enum class Signal { Offline, Listening, Thinking, Speaking, AgentAlert, Celebrate, Sulk };`
  `enum class LedPattern { Off, SoftGlow, Pulse, Chase, VoiceLevel, Blink, Breathe, TeamFlash, Rainbow, DimFlash };`
  `enum class Expression { Neutral, Attentive, Thinking, Talking, Confused, Worried, Happy, Sulky };`
  `enum class Sound { None, Chime, Nudge, Cheer, GoalHorn, Aww };`
  `struct Output { LedPattern led; uint32_t color; uint32_t alt_color; Expression face; Sound sound; bool dim; };`
  `class Attention { public: void raise(Signal, uint32_t now_ms, uint32_t color = 0, uint32_t alt = 0, uint32_t duration_ms = 0); void clear(Signal); void acknowledge(); Output output(uint32_t now_ms, bool quiet) const; Sound take_sound(bool quiet); };`
  Priority (decided, high→low): Celebrate/Sulk (timed, default 8000 ms) > AgentAlert > Listening > Speaking > Thinking > Offline > idle. Mapping per spec §6 table (Listening cyan `0x00C8FF` Pulse; Thinking blue `0x2050FF` Chase; Speaking VoiceLevel; Offline red Blink; AgentAlert Breathe agent colour; Celebrate TeamFlash then Rainbow; Sulk DimFlash red; idle SoftGlow, Off when quiet). `take_sound` returns each raised sound once and `None` during quiet hours. `acknowledge()` clears AgentAlert/Celebrate/Sulk.
- Produces (platform): `void leds_apply(const Output&, float voice_level);` (12 LEDs via `hal_io_expander`), `void expressions_apply(Expression);` (stock avatar emotions/modifiers; Celebrate also starts stock `DanceModifier`), `void sounds_play(Sound);` (via `hal_bridge::app_play_sound`).

- [ ] **Step 1: Failing tests:** `quiet hours wrap midnight` (23:00–07:00: 1380→true, 0→true, 419→true, 420→false, 720→false); `quiet hours same-day window` (13:00–15:00); `disabled quiet hours never quiet`; `celebrate outranks agent alert then expires after 8000ms`; `offline outranks idle but not agent alert`; `sound fires once`; `no sound when quiet but alert still shown`; `acknowledge clears alert and celebration`; `idle leds off when quiet`.
- [ ] **Step 2:** ctest → FAIL. **Step 3:** implement core. **Step 4:** ctest → PASS.
- [ ] **Step 5:** Implement platform adapters; ChanApp ticks `Attention` every 50 ms and applies output; head pat calls `acknowledge()`; Wi-Fi events raise/clear `Offline`. Debug-only serial command `attn <signal>` to trigger states.
- [ ] **Step 6:** On device: trigger each signal via `attn`, check LEDs/face/sound match spec §6; unplug router → red blink + confused face; set quiet hours to now → silent, dimmed.
- [ ] **Step 7:** Commit `feat: attention system with LEDs, expressions, sounds and quiet hours`.

### Task 5: Gemini Live voice

**Files:**
- Create: `chan/core/base64.{h,cpp}`, `chan/core/gemini_messages.{h,cpp}`, `tests/gemini_messages_test.cpp`, `tests/fixtures/gemini_*.json`, `chan/platform/gemini_protocol.{h,cpp}`
- Modify: `firmware/patches/xiaozhi-esp32.patch` — add `enum class AudioWire { Opus, Pcm16 }; virtual AudioWire Protocol::audio_wire() const { return AudioWire::Opus; }`; when `Pcm16`, `AudioService` skips Opus encode on the uplink (send 16 kHz mono PCM16 frames from the AFE output, AEC on) and skips Opus decode on the downlink (incoming payload is 24 kHz PCM16, played directly); the Application uses `GeminiProtocol` instead of websocket/MQTT; `kListeningModeRealtime` so barge-in works.

**Wire format (verify against current https://ai.google.dev/gemini-api/docs/live before coding; adjust fixtures if changed):** endpoint `wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent?key=<KEY>` (key never logged; log the URL with `key=***`). Client: `setup` {model `models/<id>`, generationConfig.responseModalities ["AUDIO"], speechConfig voice + languageCode, systemInstruction, tools}; `realtimeInput.audio` {data b64, mimeType `audio/pcm;rate=16000`}; `realtimeInput.video` {data b64, mimeType `image/jpeg`}; `toolResponse.functionResponses[{id,name,response}]`. Server: `setupComplete`, `serverContent.modelTurn.parts[].inlineData` (audio/pcm 24 kHz), `serverContent.interrupted`, `serverContent.turnComplete`, `toolCall.functionCalls[{id,name,args}]`, `goAway`.
Set `config.example.json` `gemini.model` to the newest Live native-audio model listed on that page at implementation time.

**Interfaces:**
- Produces (`core/base64.h`): `std::string b64_encode(const uint8_t*, size_t); bool b64_decode(std::string_view, std::vector<uint8_t>& out);`
- Produces (`core/gemini_messages.h`):
  `struct FunctionDecl { std::string name, description, params_json_schema; };`
  `struct GeminiSetup { std::string model, voice, language, system_prompt; bool google_search; std::vector<FunctionDecl> functions; };`
  `std::string build_setup(const GeminiSetup&); std::string build_audio(const int16_t* pcm, size_t samples); std::string build_image(const uint8_t* jpeg, size_t len); std::string build_tool_response(const std::string& id, const std::string& name, const std::string& response_json);`
  `struct ServerMsg { enum Kind { SetupComplete, Audio, Interrupted, TurnComplete, ToolCall, GoAway, Error, Unknown } kind; std::vector<uint8_t> audio; struct Call { std::string id, name, args_json; }; std::vector<Call> calls; std::string error; };`
  `ServerMsg parse_server_message(std::string_view json);`
  `std::string build_system_prompt(const ChanConfig&);` — name, persona, language, "short, cute spoken replies", chattiness.
- Produces (platform): `class GeminiProtocol : public Protocol` with `audio_wire() == Pcm16`; `std::function<std::string(const ServerMsg::Call&)> on_tool_call;` (set by Task 6). Session opens on wake word / `Shell::on_tap`, closes after `gemini_idle_s` without speech; `goAway` → reconnect transparently. Raises `Listening/Thinking/Speaking` on `Attention`; speech output level drives `VoiceLevel` LEDs and the stock speaking modifier. Wake word and tap are ignored during quiet hours.

- [ ] **Step 1: Failing tests:** `b64 roundtrip` incl. lengths 0,1,2,3; `setup contains model voice and googleSearch` (parse output JSON, assert `setup.model=="models/X"`, `tools[0].googleSearch` present, functionDeclarations count); `audio chunk mime is pcm 16k`; `parses setupComplete`; `parses audio part into pcm bytes` (fixture); `parses interrupted`; `parses tool call with two calls`; `unknown message is Unknown not crash`; `system prompt contains name and language`.
- [ ] **Step 2:** ctest → FAIL. **Step 3:** implement. **Step 4:** ctest → PASS.
- [ ] **Step 5:** Implement the xiaozhi patch + `GeminiProtocol`; regenerate patch file.
- [ ] **Step 6:** On device: "Hi Stack-chan, ¿qué hora es en Tokio?" → answers by voice, cyan while listening, blue while thinking, mouth + LEDs while speaking; talk over it → it stops (barge-in); silence 20 s → session closes, back to idle; ask a current-events question → answer uses search. Check serial log never shows the key.
- [ ] **Step 7:** Commit `feat: Gemini Live voice with barge-in and search`.

### Task 6: Model tools — camera, robot, status

**Files:**
- Create: `chan/core/tools.{h,cpp}`, `tests/tools_test.cpp`, `chan/platform/camera_tool.{h,cpp}`
- Modify: `chan/platform/chan_app.cpp` (register tools, wire `GeminiProtocol::on_tool_call`)

**Interfaces:**
- Produces (`core/tools.h`):
  `using ToolFn = std::function<std::string(const std::string& args_json)>;` (returns response JSON object)
  `class ToolRegistry { public: void add(FunctionDecl, ToolFn); std::vector<FunctionDecl> declarations() const; std::string dispatch(const std::string& name, const std::string& args_json) const; };` — unknown tool → `{"error":"unknown tool <name>"}`; thrown/invalid args → `{"error":…}`.
- Tools (decided names): `look` (no args; captures one JPEG ≤ 640×480, sends via `build_image`, returns `{"ok":true}`), `set_head_angles` {yaw, pitch}, `get_head_angles`, `set_expression` {expression: one of Expression names}, `dance` {seconds ≤ 10}, `show_screen` {screen: face|agents|sports}, `get_agent_status` (from Task 7 store; returns `[]` until then), `get_scores` (Task 9), `get_weather` (Task 8). Google Search via `GeminiSetup.google_search = true`.
- Consumes: `GeminiProtocol::on_tool_call`, `Shell::show`, `expressions_apply`, stock `motion` for angles.

- [ ] **Step 1: Failing tests:** `declarations lists added tools`; `dispatch calls tool with args`; `unknown tool returns error json`; `tool exception becomes error json`.
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5:** Implement `camera_tool` (stock `stackchan_camera` capture → JPEG via `esp_new_jpeg`); register all tools.
- [ ] **Step 6:** On device: "¿Qué tengo en la mano?" → turns on camera, describes object; "mira a la izquierda" → head turns; "enséñame los agentes" → Agents screen.
- [ ] **Step 7:** Commit `feat: camera and robot tools for Gemini`.

### Task 7: Agent status — HTTP API, store, screen, hooks

**Files:**
- Create: `chan/core/agents.{h,cpp}`, `tests/agents_test.cpp`, `chan/platform/http_server.{h,cpp}`, `chan/platform/screens/agents_screen.cpp`, `hooks/chan_notify.py`, `hooks/test_chan_notify.py`, `hooks/README.md`

**API (decided):** `GET /api/health` → `{"ok":true,"name":…}` (no auth). `POST /api/agents` with `Authorization: Bearer <token>`, body ≤ 4096 bytes: `{"agent":"claude-code"|"codex"|"gemini"|<any>,"session":"<id>","project":"<dir name>","state":"working"|"needs_attention"|"done"|"ended","message":"<≤120 chars>"}` → 204; bad token → 401; bad body → 400 `{"error":…}`. mDNS `<hostname>.local`, service `_chan._tcp`.

**Interfaces:**
- Produces (`core/agents.h`):
  `enum class AgentState { Working, NeedsAttention, Done };`
  `struct AgentEvent { std::string agent, session, project, state, message; };`
  `bool parse_agent_event(std::string_view json, AgentEvent& out, std::string& error);`
  `struct AgentRow { std::string agent, session, project, message; AgentState state; uint32_t since_ms; bool acknowledged; };`
  `enum class Change { None, BecameNeedsAttention, Other };`
  `class AgentStore { public: AgentStore(int max_sessions, uint32_t stale_ms, uint32_t renudge_ms); Change apply(const AgentEvent&, uint32_t now_ms); std::vector<AgentRow> rows(uint32_t now_ms) const; bool any_unacknowledged_attention() const; bool take_renudge(uint32_t now_ms); void acknowledge_all(); std::string to_json(uint32_t now_ms) const; };`
  Rules: key = agent+session; `ended` removes the row; rows older than stale are dropped from `rows()`; beyond max → evict oldest-updated; `take_renudge` true exactly once per alert after `renudge_ms` if not acknowledged; rows sorted needs-attention first then most recent.
- Wiring: `BecameNeedsAttention` → `Attention::raise(AgentAlert, color=agent colour)` + `Sound::Chime`, head turns to centre-front; renudge → `Sound::Nudge`; head pat / any voice turn → `acknowledge_all()` + `Attention::acknowledge()`; `get_agent_status` tool returns `to_json`.
- Hooks: `python3 hooks/chan_notify.py --from claude|codex|gemini|generic [--state S --agent A]` reads hook JSON (stdin for Claude/Gemini, argv[1] for Codex), env `CHAN_HOST` (default `chan.local`) and `CHAN_AGENT_TOKEN`, timeout 1 s, always exit 0. Mapping (verify each against current docs): Claude Code `Notification`→needs_attention, `UserPromptSubmit`→working, `Stop`→done, `SessionEnd`→ended (session = `session_id`, project = basename of `cwd`); Codex `notify` `agent-turn-complete`→done (plus any approval-type event the current Codex exposes → needs_attention); Gemini CLI hooks equivalent events.

- [ ] **Step 1: Failing C++ tests:** `parses valid event`; `rejects unknown state`; `rejects missing agent`; `rejects body over 4096 bytes` (parse fn guards size); `needs_attention reports change once`; `ended removes row`; `stale rows hidden after stale_ms`; `store evicts oldest beyond max sessions`; `renudge fires once after delay`; `acknowledge stops renudge`; `attention rows sort first`.
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5: Failing pytest:** `test_claude_notification_maps_to_needs_attention`, `test_claude_stop_maps_to_done`, `test_codex_turn_complete_maps_to_done`, `test_project_is_cwd_basename`, `test_notify exits 0 when host unreachable` (point at `127.0.0.1:9`, assert exit 0 and elapsed < 1.5 s), `test_token_sent_as_bearer` (local `http.server` stub).
- [ ] **Step 6:** Implement `chan_notify.py`; pytest PASS. Write `hooks/README.md` with exact snippets for `~/.claude/settings.json`, `~/.codex/config.toml`, Gemini CLI settings.
- [ ] **Step 7:** Implement `http_server` (esp_http_server, constant-time token compare) + mDNS + Agents screen (rows: agent colour dot, project, state, "3m"). 
- [ ] **Step 8:** On device: `curl -H "Authorization: Bearer $CHAN_AGENT_TOKEN" -d '{"agent":"claude-code","session":"t1","project":"chan","state":"needs_attention"}' http://chan.local/api/agents` → chime, orange breathing, worried face, row on Agents screen; wait 5 min → one nudge; pat head → calm. Wrong token → 401. Then wire real Claude Code hooks on the Mac and confirm a permission prompt lights Chan up.
- [ ] **Step 9:** Commit `feat: agent status API, screen and hooks`.

### Task 8: Clock and weather

**Files:**
- Create: `chan/core/weather.{h,cpp}`, `tests/weather_test.cpp`, `tests/fixtures/ipwho.json`, `tests/fixtures/open_meteo.json`, `chan/platform/http_fetch.{h,cpp}`, `chan/platform/screens/face_overlay.cpp`

**Interfaces:**
- Produces (`core/weather.h`):
  `struct Location { double lat, lon; std::string city, timezone; };`
  `std::optional<Location> parse_ipwho(std::string_view json);` (requires `success:true`; request `https://ipwho.is/?fields=success,city,latitude,longitude,timezone`)
  `struct Weather { double temp; int code; bool is_day; double high, low; };`
  `std::optional<Weather> parse_open_meteo(std::string_view json);` (request `https://api.open-meteo.com/v1/forecast?latitude=..&longitude=..&current=temperature_2m,weather_code,is_day&daily=temperature_2m_max,temperature_2m_min&timezone=auto&forecast_days=1`, `&temperature_unit=fahrenheit` when `units=="imperial"`)
  `enum class WeatherIcon { Clear, PartlyCloudy, Cloudy, Fog, Drizzle, Rain, Snow, Storm };` `WeatherIcon icon_for_code(int wmo_code);`
- Produces (`platform/http_fetch.h`): `bool http_get_json(const std::string& url, JsonDocument& out, const JsonDocument* filter, std::string& error, size_t max_bytes = 512*1024);` — TLS with the ESP-IDF cert bundle, body buffered in PSRAM, ArduinoJson `DeserializationOption::Filter` when given.
- Behaviour: location once per boot (retry with backoff); weather every 15 min; failure keeps last value. Timezone `auto` → use `Location.timezone` (map IANA → POSIX TZ via a small table for the owner's zones plus a UTC fallback; documented). NTP via `esp_sntp`. Face overlay: small clock + icon + temp, only when idle (hidden during Listening/Speaking/alerts).

- [ ] **Step 1: Failing tests:** `parses ipwho fixture` (city, lat/lon); `ipwho success false is nullopt`; `parses open-meteo fixture`; `wmo 0 is Clear, 61 is Rain, 95 is Storm, 71 is Snow, 45 is Fog`.
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5:** Implement fetch + overlay + `get_weather` tool. On device: idle face shows correct local time and current weather; "¿Qué tiempo hace?" answers consistently with the overlay.
- [ ] **Step 6:** Commit `feat: idle clock and weather`.

### Task 9: Live sports and celebrations

**Files:**
- Create: `chan/core/sports.{h,cpp}`, `tests/sports_test.cpp`, `tests/fixtures/espn_{soccer,nfl,nba,nhl}.json`, `tests/fixtures/mlb_schedule.json` (capture real payloads; trim to ≤ 200 KB each), `chan/platform/screens/sports_screen.cpp`

**Sources (decided, same as Dash):** ESPN `https://site.api.espn.com/apis/site/v2/sports/<sport>/<league>/scoreboard` (`soccer/<league>`, `football/nfl`, `basketball/nba`, `hockey/nhl`); MLB `https://statsapi.mlb.com/api/v1/schedule?sportId=1&date=<YYYY-MM-DD>&hydrate=linescore`.

**Interfaces:**
- Produces (`core/sports.h`):
  `enum class Sport { Soccer, Football, Basketball, Hockey, Baseball };` `Sport sport_for_league(std::string_view league);` (`nfl`→Football, `nba`→Basketball, `nhl`→Hockey, `mlb`→Baseball, else Soccer)
  `enum class GameStatus { Scheduled, Live, Final };`
  `struct Side { std::string name, abbrev; int score; };`
  `struct Game { std::string id, league; Side home, away; GameStatus status; std::string clock; uint64_t start_epoch; };`
  `std::vector<Game> parse_espn_scoreboard(std::string_view json, const std::string& league);`
  `std::vector<Game> parse_mlb_schedule(std::string_view json);`
  `const JsonDocument& espn_filter(); const JsonDocument& mlb_filter();` (ArduinoJson filters keeping only needed fields)
  `bool game_involves(const Game&, const Team&);` (match on name/displayName/abbrev, case-insensitive)
  `struct ScoreEvent { std::string game_id, team_key; bool ours; int points; Sport sport; std::string line; };` (`line`: "GOAL!", "TOUCHDOWN!", "FIELD GOAL!", "SAFETY!", "BASKET!" (2) / "THREE!" (3) / "FREE THROW!" (1), "RUN!" — derived from sport and points only; baseball is always "RUN!" in v1)
  `class ScoreTracker { public: std::vector<ScoreEvent> update(const std::vector<Game>& games, const std::vector<Team>& followed); std::vector<Game> followed_games() const; };`
  Rules: first observation of a game → no events; score increase on a followed side → `ours=true`; increase on the opponent of a followed team → `ours=false`; decrease → no event, state updated; same snapshot twice → no new events; failed poll (caller passes nothing) keeps previous games; games where both sides are followed → `ours=true` for the scorer.
- Wiring: poller per distinct league in `teams`: every 15 s while any followed game is Live, 10 min otherwise, 60 min if no followed game today. `ours` → `Attention::raise(Celebrate, team.color, team.alt_color)` + `Sound::GoalHorn` (soccer/hockey) or `Sound::Cheer` + Sports screen for 8 s with `line`; not ours → `Sulk` + `Sound::Aww`. `get_scores` tool returns followed games JSON.

- [ ] **Step 1: Failing tests:** `parses real scoreboard fixture with filter` for each ESPN fixture (count > 0, scores ≥ 0, status mapped); `parses mlb fixture`; `no event on first observation`; `our goal fires ours event with GOAL line`; `opponent score fires sulk event`; `no event on score decrease`; `same score twice fires once`; `touchdown is 6 points line TOUCHDOWN`; `tracker keeps last games when a poll fails`; `game_involves matches abbrev case-insensitively`.
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5:** Implement pollers + Sports screen (followed games: crest-colour chips, score, clock; live first).
- [ ] **Step 6:** On device: debug serial command `fake-score <league> <team_key>` injects a synthetic snapshot → celebration (team-colour flash → rainbow, dance, goal horn, "GOAL!") and a sulk for the opponent; during quiet hours → visuals only. Then watch one real live game.
- [ ] **Step 7:** Commit `feat: live sports with celebrations`.

### Task 10: OTA push from the Mac

**Files:**
- Modify: `chan/platform/http_server.cpp` (`POST /api/ota`, bearer token, streams body into the inactive OTA slot with `esp_ota_*`, validates image, sets boot partition, reboots; 409 if a Gemini session is active), `hal/hal_ota.cpp` (remove xiaozhi pull/check flow), `scripts/chan.py` (`ota` subcommand uploads `firmware/build/stack-chan.bin` with the token from `.env`), `scripts/test_chan.py`

**Interfaces:** `python3 scripts/chan.py ota [--host chan.local]` → prints progress, exits non-zero on non-2xx. Rollback: enable `CONFIG_BOOTLOADER_APP_ROLLBACK_ENABLE`; ChanApp marks the app valid after Wi-Fi + config load succeed.

- [ ] **Step 1: Failing pytest:** `test_ota_sends_bearer_and_binary` (stub server), `test_ota_fails_on_401`.
- [ ] **Step 2–4:** FAIL → implement → PASS.
- [ ] **Step 5:** On device: bump a version string, `chan.py ota` → robot reboots into the new version; upload garbage → rejected, robot keeps running; wrong token → 401.
- [ ] **Step 6:** Commit `feat: authenticated OTA push`.

### Task 11: Docs, CI hardening, open-source readiness

**Files:**
- Modify: `README.md` (Dash-style: what it does, hardware, quick start — install IDF, `cp .env.example .env`, `cp config.example.json config.json`, `chan.py build flash flash-config`; agent hooks; restore stock; credits), `.github/workflows/ci.yml` (add secret scan job using upstream `scan_secrets.py` adapted to the repo root, plus `gitleaks` action), `NOTICE`
- Create: `docs/config.md` (every key in both files), `docs/architecture.md` (core/platform split, xiaozhi patch, config partition, attention priority), `CONTRIBUTING.md`, `SECURITY.md` (keys live in device flash — owner-accepted risk; how to rotate)

- [ ] **Step 1:** Write docs; every `config.example.json` / `.env.example` key appears in `docs/config.md` (add pytest `test_every_config_key_documented`).
- [ ] **Step 2:** CI: all jobs green (host-tests, pytest, secret scan, firmware-build).
- [ ] **Step 3:** Fresh-clone dry run on the Mac following only the README: build, flash, config, talk, agent alert.
- [ ] **Step 4:** Commit `docs: README, config reference, architecture, security`.
