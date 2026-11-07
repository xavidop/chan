# Configuration reference

Chan is configured by two files in the repo root, both **gitignored**; the committed
`.env.example` and `config.example.json` show every key. Changing either file does not need a
firmware rebuild — `python3 scripts/chan.py flash-config` packs them into the robot's `chancfg`
partition (64 KB at 0xE10000, CRC-checked) and the robot reads them at boot.

## `.env` — secrets and network

| Key | Required | What it is |
|---|---|---|
| `WIFI_SSID` | yes | 2.4 GHz network the robot joins (the ESP32-S3 has no 5 GHz) |
| `WIFI_PASSWORD` | | Wi-Fi password |
| `GEMINI_API_KEY` | yes | Gemini API key for the Live voice model — <https://aistudio.google.com/apikey> |
| `CHAN_AGENT_TOKEN` | yes | Shared secret your coding-agent hooks send as `Authorization: Bearer …`; also protects OTA. Any long random string (`openssl rand -hex 24`) |
| `CHAN_HOST` | | Optional, read by the hook script only: the robot's host/IP when `chan.local` does not resolve on your machine |
| `OPENAI_API_KEY` | | Reserved for future features, unused in v1 |
| `GITHUB_TOKEN` | | Reserved, unused in v1 |
| `ANTHROPIC_API_KEY` | | Reserved, unused in v1 |

Keys end up in the robot's flash. Treat the device like a key holder (see [SECURITY.md](../SECURITY.md)).

## `config.json` — everything else

| Key | Default | What it does |
|---|---|---|
| `name` | `"Chan"` | The robot's name; used in the persona and the health endpoint |
| `language` | `"es"` | Language it speaks and listens in (`es`, `en`, `pt`, `fr`, `de`, `it`, `ja`, `zh`, or a full BCP-47 code like `en-GB`) |
| `timezone` | `"auto"` | `auto` = from your public IP location; otherwise an IANA zone (`Europe/Madrid`) or a POSIX TZ string |
| `units` | `"metric"` | `metric` (°C) or `imperial` (°F) |
| `hostname` | `"chan"` | mDNS/DHCP hostname → `chan.local` |
| `persona.prompt` | | Free-text personality added to the system prompt |
| `persona.chattiness` | `1` | `0` terse (one short sentence), `1` normal, `2` chatty (out-of-range values are clamped) |
| `gemini.model` | `"gemini-3.8-live"` | Live API model id (`models/` prefix added by Chan). Must support `bidiGenerateContent` |
| `gemini.voice` | `"Aoede"` | Prebuilt voice name; empty for the server default |
| `gemini.sessionIdleSeconds` | `20` | Seconds of silence (nobody speaking, nothing playing) before the voice session closes. Range 5–600 |
| `quietHours.enabled` | `true` | Quiet hours mute sounds and voice wake and dim the LEDs; screens keep updating |
| `quietHours.start` | `"23:00"` | Quiet hours start, `HH:MM` local time |
| `quietHours.end` | `"07:00"` | Quiet hours end, `HH:MM`; the window may wrap midnight |
| `volume` | `70` | Speaker volume 0–100, applied at boot (Settings → Volume tweaks it live) |
| `brightness` | `70` | Backlight 0–100, applied at boot |
| `screens.agents` | `true` | Show the Agents screen in the swipe cycle |
| `screens.sports` | `true` | Show the Sports screen and poll scores |
| `screens.weather` | `true` | Show the Weather screen and the idle clock/weather on the face |
| `agents.maxSessions` | `8` | Most agent sessions kept (1–32); the least recently updated is evicted |
| `agents.staleMinutes` | `120` | Rows not updated for this long disappear and stop alerting (1–1440) |
| `agents.renudgeMinutes` | `5` | One extra nudge if an alert is not acknowledged after this long (0–120; `0` disables the re-nudge) |
| `agents.colors` | Claude `#D97757`, Codex `#10A37F`, Gemini `#4285F4`, Copilot `#8957E5` | LED/row colour per agent name (`claude-code`, `codex`, `gemini`, `copilot`, or any name your hooks send) |
| `teams` | `[]` | Array of teams to follow. Same shape as Dash: `key` (short id), `league` (ESPN slug such as `esp.1`, `usa.1`, `uefa.nations`, `nfl`, `nba`, `nhl`, or `mlb`), `name` (as the feed names it), `displayName`, `color`, `altColor` |

Numbers outside their range are clamped to the nearest bound rather than rejected, so a typo
cannot leave the robot silent or blinding.

Leagues: soccer uses ESPN league slugs (`esp.1` La Liga, `eng.1` Premier League, `usa.1` MLS,
`uefa.champions`, `uefa.nations`, …); `nfl`, `nba`, `nhl` use ESPN; `mlb` uses the MLB Stats API.

### How often Chan talks to the internet

| What | When |
|---|---|
| Location (ipwho.is) | once per boot (retried every minute until it works) |
| Weather (Open-Meteo) | every 15 minutes |
| Live scores | every 15 s while a followed team is playing, every 10 min when one plays later today, otherwise hourly |
| Last results | once a day: the last 3 days' scoreboards, then each still-missing team's season schedule |
| Gemini Live | only while a voice session is open (wake word or tap) |
