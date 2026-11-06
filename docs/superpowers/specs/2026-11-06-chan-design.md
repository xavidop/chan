# Chan — design

Date: 2026-11-06 · Owner: Xavier Portilla Edo · Status: draft for approval

## 1. What this is, and why

Chan is replacement firmware for the **official M5Stack Stack-chan** (CoreS3: ESP32-S3, 16 MB flash, 8 MB PSRAM,
320×240 touch screen, 0.3 MP camera, dual mics, 1 W speaker; body with two Feetech feedback servos, 12 RGB LEDs,
3-zone head touch panel, IR, NFC, battery).

It turns the robot into a **cute, vibrant desk companion** that:

- you talk to with **Gemini's latest realtime (Live) model**, which can **search the web** and **look through the camera**;
- tells you when your **coding agents** (Claude Code, Codex, Gemini CLI, …) **need you**;
- follows your **teams live** and **physically celebrates** a goal / run / touchdown / basket;
- shows a **clock and weather** while idle.

It is the small sibling of [Dash](https://github.com/xavidop/dash) (a wall-tablet smart display): same spirit —
open source, one config, everything configurable — but scoped down to what a 2-inch companion is good at.

Repo: `github.com/xavidop/chan` (private for now, MIT, open-sourced later). Local: `~/Documents/personal/repos/chan`.

## 2. Goals, non-goals, anti-goals

**Goals**
- Natural voice conversation with Gemini Live, with interruption (barge-in) and the robot's face/motion reacting.
- Web search and camera ("look") available to the model as tools.
- An agent-status screen and attention alerts for Claude Code, Codex and Gemini CLI (extensible to others).
- Live scores for soccer, NFL, NBA, NHL and MLB with celebrations for the user's teams.
- Idle clock + weather (location from the public IP).
- Vibrant: LEDs, expressions, head pats and motion used for every state, like the stock firmware.
- Everything configurable from two files; no rebuild needed to change config.

**Non-goals**
- Recipes, notes, reminders, calendar, news, Spotify/music, finance — anything Dash does that needs a big screen.
- A companion server/hub on the computer. The robot talks to the internet and to your agents directly.
- Approving agent actions from the robot (status and alerts only, v1).
- Custom wake word (use the stock "Hi Stack-chan").
- Our own phone app or our own backend server.
- Phone-app video/remote control (M5Stack's app can't pair with open-source builds — see §3). Revisit later,
  possibly as a web page served by Chan.
- Chan speaking unprompted: celebrations and nudges use sound clips, not Gemini.

**Anti-goals** (would be failures even if it "works")
- Any secret committed to git or printed in logs.
- Chan talking or making noise unprompted outside the four triggers in §4.
- Nagging: alerts that repeat forever, or celebrations that can't be quieted with a pat.
- Losing the ability to go back to stock firmware (backup exists in `backup/`, gitignored).

## 3. Foundation (decided)

Fork the **official firmware** from [m5stack/StackChan](https://github.com/m5stack/StackChan) `firmware/` (MIT).
It is C++ on **ESP-IDF 6.1**, built on **xiaozhi-esp32 v2.5.2** (plus M5Stack's patch), **mooncake** (app framework),
**smooth_ui_toolkit** (UI/animation) and LVGL 9. Dependencies are fetched and pinned by `fetch_repos.py` /
`repos.json` with patches applied on top — Chan keeps that model and adds its own patches, so upstream fixes can be
pulled in later.

Credit M5Stack and the xiaozhi project in README/NOTICE; keep their copyright headers.

**Keep from stock**
- HAL and drivers: servos (incl. NVS calibration), LEDs (PY32 IO expander), head touch (Si12T), IMU, RTC, camera,
  display, battery.
- Face and motion engine (`stackchan/`: avatar, motion, modifiers — blink, breath, idle motion/expression, speaking,
  dance, head pet, IMU events, timed emotion/speech).
- xiaozhi audio engine: AFE with echo cancellation, WakeNet "Hi Stack-chan" (`CONFIG_SR_WN_WN9L_HISTACKCHAN_TTS3`),
  audio codec drivers, Wi-Fi station code.
- Settings screen, trimmed to: Wi-Fi, volume, brightness, servo calibration, about.
- OTA updates, reworked as **push from the Mac** (my call): an upload endpoint on Chan's LAN HTTP server, protected
  by `CHAN_AGENT_TOKEN`, plus a script in the repo. The stock xiaozhi check-version/pull flow is removed (it depends
  on the removed server and could pull stock firmware over Chan).

**Remove**
- xiaozhi cloud protocol/server and its account/activation flow (the voice backend becomes Gemini).
- Phone-app video/avatar and pairing (`hal_ws_avatar`, `hal_account`, `local_avatar_client`, BLE pairing,
  `secret_logic`): the real auth code is closed-source (the open repo ships placeholder stubs) and local mode needs
  M5Stack's cloud to hand over the app's address, so it cannot work in an open build. Dropped for v1 (owner's call).
- App center, EzData, ESP-NOW remote, dance app as a separate app (dance stays as a behaviour), reminders and the
  `create/get/stop_reminder` tools.
- Stock `server/`, `app/`, `remote/` folders (not brought into this repo).

## 4. When Chan gets your attention (decided)

Only these four, everything else is silent:

1. **Wake word** "Hi Stack-chan" → perks up, listens.
2. **Touching the screen** → same as the wake word.
3. **An agent needs you** (permission/input requested) → turns toward you, worried face, chime, LEDs breathe in that
   agent's colour. If ignored: one re-nudge after a few minutes, then it stops nagging (stays visible on screen/LEDs).
   An agent simply *finishing* only updates the agent screen, silently.
4. **A sports event** for a followed team → celebrate (or sulk if it's the opponent), briefly show the score.

**Quiet hours / do-not-disturb** in config mute chimes, voice and celebrations; screens and LEDs (dimmed) still
update. Defaults mirror Dash: 23:00–07:00.

Chan never starts talking on its own.

## 5. Screens

Switched by voice ("show me the agents"), by a horizontal swipe on the screen, or by swiping across the head panel.
Touch input (my call): **tap anywhere = start talking** (same as the wake word) on every screen; **horizontal swipe =
next/previous screen**; vertical swipe scrolls lists on Agents/Sports. Acknowledging an alert is a head pat (§6) or
any voice turn.

1. **Face (home).** Animated avatar; expression follows mood and what it's saying; mouth moves with speech. When
   idle, a small **clock + current weather** sits with the face (exact layout: builder's call, should stay cute and
   uncluttered).
2. **Agents.** One row per watched agent/session: name, agent icon/colour, state (working / done / needs you),
   how long ago. Rows that need attention are highlighted.
3. **Sports.** Live and upcoming games for followed teams, live score and clock/period; after a scoring event the
   screen pops here for a few seconds then returns.
4. **Settings** (from stock, trimmed — §3).

## 6. Lights, head pats, motion (decided direction; exact values are builder's call)

| Moment | LEDs | Face / motion |
|---|---|---|
| Listening | soft cyan pulse | attentive, looks at user |
| Thinking / connecting | blue chase | thinking face |
| Speaking | pulses with voice level | mouth moves (stock speaking modifier) |
| Wi-Fi / internet down | red blink | sad/confused, status indicator |
| Agent needs you | breathe in agent colour (Claude orange, Codex green, Gemini blue; configurable) until acknowledged | turns toward user, worried |
| Your team scores | flash team colours (from config), then rainbow | dance + happy, sport-specific line ("GOAL!", "Touchdown!", "Home run!") |
| Opponent scores | short dim red | sulk |
| Idle | very soft glow; off during quiet hours | idle motion/expression, blink, breath |

**Head pats:** a pat → happy giggle/squint (stock head-pet behaviour). A pat during an agent alert or a celebration
→ acknowledge and quiet it. A front-to-back swipe → next screen.

## 6a. Sounds (decided)

Celebrations, sulks, agent alerts and re-nudges use **short bundled sound clips** (cheers, chimes, sulky "aww")
stored in the assets partition, plus the on-screen line ("GOAL!", "Touchdown!", "Home run!"). No Gemini session is
opened for these — instant, offline, free, and Chan still never *talks* unprompted. Clip choice is the builder's
call; sources must be license-clean for open source.

## 7. Voice: Gemini Live (decided: direct from the device)

- Device opens a WebSocket (TLS) straight to the **Gemini Live API** using `GEMINI_API_KEY` from config. Implemented
  as a new xiaozhi `Protocol` subclass replacing the websocket/MQTT ones.
- **Model:** the latest Gemini realtime/Live model with native audio at build time; the model ID lives in config so
  it can be bumped without code changes. Voice name and language are configurable (user's Dash runs in Spanish;
  default language comes from config).
- **Audio:** xiaozhi Opus-encodes mic audio; Gemini Live wants raw PCM16 (16 kHz in) and returns PCM (24 kHz out —
  matches the board's 24 kHz output). Patch the audio service to send/receive raw PCM on this path. Echo
  cancellation stays on so barge-in works.
- **Session lifecycle:** session opens on wake word / screen touch, stays open for follow-ups, closes after a
  configurable idle timeout. Interruption: user speech during playback stops playback.
- **Tools given to the model:**
  - Google Search (Gemini's built-in grounding tool) — "access the web".
  - `look` — captures one JPEG from the camera and sends it to the session. Camera is not streamed continuously.
  - Robot tools from the stock MCP set: `set_head_angles`, `get_head_angles`; plus `set_expression`, `dance`,
    `show_screen` (face / agents / sports), and read-only `get_agent_status`, `get_scores`, `get_weather` so it can
    answer "do my agents need me?" / "how are the Raptors doing?".
- **Persona:** name, personality prompt, chattiness — from config. Cute, short spoken replies by default.

## 8. Agent status (decided: agents call the robot directly)

- Chan runs a small HTTP server on the LAN, advertised via mDNS as `chan.local` (hostname configurable).
- Requests must carry a shared token (`CHAN_AGENT_TOKEN` in `.env`) — otherwise anyone on the Wi-Fi could trigger
  alerts.
- The repo ships **hook scripts and setup docs** for:
  - **Claude Code** — `Notification` (needs permission/input → `needs_attention`), `Stop` (→ `done`), and
    session start/prompt submit (→ `working`).
  - **Codex** — its `notify` config.
  - **Gemini CLI** — its hooks.
  - A generic `curl` example so any other agent can be added.
- Payload identifies agent, session/project (so several Claude Code sessions show as separate rows), state and an
  optional short message. Device keeps state in RAM; stale sessions age out.
- Hook scripts fail silently and fast if the robot is off, so they never slow down the agents.

## 9. Sports (decided: soccer, NFL, NBA, NHL, MLB)

- Device polls the same free sources Dash uses: ESPN public site API (`site.api.espn.com`) for soccer/NFL/NBA/NHL,
  MLB Stats API (`statsapi.mlb.com`) for MLB. Parse with filtered/streamed JSON (ArduinoJson filter, PSRAM) to keep
  memory sane.
- Followed teams in config with league, display name and colours — same shape as Dash's `favorites.teams`
  (e.g. Valencia CF / esp.1, Toronto Blue Jays / mlb, Toronto Raptors / nba, Toronto FC / usa.1, Spain).
- Poll slowly when no game is on; fast during live games of followed teams. Detect scoring events by score diffs
  and fire the celebration/sulk. A refresh failure keeps the last data; never celebrate a score twice (e.g. after a
  reconnect).

## 10. Clock and weather

- Time from NTP, timezone from config (default from IP lookup). Stock RTC keeps time across reboots.
- Location from the public IP (`ipwho.is`, as Dash does), weather from **Open-Meteo** (no key). Refresh every ~15
  min. Units configurable.

## 11. Configuration and secrets (decided)

Two files in the repo root, **both gitignored**, with committed examples:

- `.env` — secrets and network: `WIFI_SSID`, `WIFI_PASSWORD`, `GEMINI_API_KEY`, `CHAN_AGENT_TOKEN`, plus room for
  future ones (`OPENAI_API_KEY`, `GITHUB_TOKEN`, …) even if v1 doesn't use them. `.env.example` documents each.
- `config.json` — everything else: name, language, voice, model ID, persona, chattiness, quiet hours, volume,
  brightness, teams, agent colours, enabled features/screens, mDNS hostname. `config.example.json`
  documents each, Dash-style.

A flash script packs both into a dedicated config partition on the device, so changing config is a quick upload,
not a firmware rebuild. Keys therefore live in the robot's flash — **accepted by the owner** (it's a personal
device; no hub).

Hygiene: secret scanning in CI (stock repo already has `scan_secrets.py` as a starting point), keys never logged,
`backup/` gitignored.

## 12. Repo and tooling

- ESP-IDF 6.1 (stock toolchain). `fetch_repos.py` + `repos.json` + `patches/` for upstream deps.
- Simple commands documented in README: fetch deps, build, flash firmware, flash config, monitor logs, restore stock.
- CI: build the firmware, run host-side tests (stock already has a host test target for motion math), secret scan.
- MIT license (done), README modelled on Dash's.

## 13. Left to the builder

- Exact layouts, fonts, icons, animation timings, LED colours/brightness curves, chime sounds.
- Config partition format and size, and how the flash script writes it.
- JSON payload shape for the agent endpoint, and how long stale agent sessions live.
- Poll intervals, Gemini session idle timeout, re-nudge delay.
- How to structure the Chan code relative to the stock tree (new app folders vs. patches), as long as upstream
  updates stay feasible.
- Order of work, as long as each step leaves a flashable, working robot.
