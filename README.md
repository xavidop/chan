# Chan

**A cute, configurable desk companion firmware for the official [M5Stack Stack-chan](https://shop.m5stack.com/products/stackchan-kawaii-co-created-open-source-ai-desktop-robot).**
Talk to it (Gemini Live, with web search and its own camera), see when your coding agents need you,
follow your teams and watch it celebrate every goal, and glance at the clock and weather while it idles.

Sibling of [Dash](https://github.com/xavidop/dash), the wall-tablet smart display: same spirit — open
source, one config, everything configurable — scoped to what a 2-inch robot on your desk is good at.

## What it does

- **Voice.** "Hi Stack-chan" (or a tap) opens a live conversation with Gemini's realtime model in your
  language. It can search the web, take a photo to see what you're holding, turn its head, change its
  face, dance and switch screens — all as tools the model calls. Talk over it and it stops.
- **Agents.** Claude Code, Codex, Gemini CLI and Copilot CLI report through tiny hook scripts. When one
  needs your permission or input, Chan chimes, breathes that agent's colour, looks worried and turns
  to you; the Agents screen lists every session. A pat on the head, or talking to it, acknowledges.
- **Sports.** Live scores for your teams (soccer, NFL, NBA, NHL, MLB). A goal, touchdown, three or
  run for your team: team-colour flash → rainbow, a dance, a horn or cheer and a big **GOAL!**; the
  other side scores: a short sulk. When nothing is on, the Sports screen shows each team's last result.
- **Weather and clock** from your IP location on the idle face, plus a Weather screen.
- **Vibrant.** 12 LEDs, expressions, head motion and sound clips for every state; quiet hours at night.
- **Configurable like Dash.** `.env` for secrets and Wi-Fi, `config.json` for everything else; change
  either and re-upload, no rebuild needed.

## Hardware

The **official M5Stack Stack-chan** (CoreS3: ESP32-S3, 16 MB flash, 8 MB PSRAM, 320×240 touch screen,
camera, dual mics, speaker; body with two servos, 12 RGB LEDs and a touch panel on the head). Other
Stack-chan builds are not supported by this firmware.

## Quick start

Requirements: macOS/Linux, Python 3, [ESP-IDF v6.1](https://docs.espressif.com/projects/esp-idf/en/v6.1/esp32s3/get-started/index.html)
(`python3 scripts/chan.py setup` prints the install steps), a Gemini API key, a USB-C data cable.

```bash
git clone https://github.com/xavidop/chan && cd chan
cp .env.example .env                  # Wi-Fi, GEMINI_API_KEY, CHAN_AGENT_TOKEN
cp config.example.json config.json    # name, language, teams, quiet hours…
```

```bash
# back up the stock firmware first (16 MB, a few minutes) — keep it, it's your way back
python3 -m esptool --port /dev/cu.usbmodem101 --baud 921600 read-flash 0 0x1000000 backup/stackchan-stock-16MB.bin
```

```bash
. ~/esp/esp-idf/export.sh
(cd firmware && python3 fetch_repos.py)   # pinned upstream deps + patches
python3 scripts/chan.py build
python3 scripts/chan.py flash --port /dev/cu.usbmodem101
python3 scripts/chan.py flash-config --port /dev/cu.usbmodem101
```

Say "Hi Stack-chan". Swipe left for Agents, Sports and Weather; hold the screen for 2 s for Settings
(Wi-Fi status, volume, brightness, servo calibration). Later updates can go over Wi-Fi:
`python3 scripts/chan.py ota`.

Wire your agents: [hooks/README.md](hooks/README.md) (or run `python3 hooks/install_hooks.py`).
Every setting: [docs/config.md](docs/config.md). How it's built: [docs/architecture.md](docs/architecture.md).

Going back to stock: `python3 scripts/chan.py restore-stock --port /dev/cu.usbmodem101`.

## Status

Working end to end on one robot (voice, tools, agents, sports, weather, OTA). Rough edges are listed in
the design doc's rulings and in open issues. Private while it settles; MIT once public.

## Credits

Built on the official [m5stack/StackChan](https://github.com/m5stack/StackChan) firmware (MIT) and the
[xiaozhi-esp32](https://github.com/78/xiaozhi-esp32) voice runtime (MIT) — see [NOTICE](NOTICE). Stack-chan
was created by Shinya Ishikawa and grown by its community. Sound clips are synthesized in this repo.
Data: ESPN and MLB public APIs, [Open-Meteo](https://open-meteo.com), [ipwho.is](https://ipwho.is).

MIT © Xavier Portilla Edo
