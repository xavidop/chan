# Security

## What lives on the robot

`.env` (Wi-Fi password, Gemini API key, the agent/OTA token) is written to the robot's flash in
plain text. The robot is a personal device on your desk: anyone with physical access and a USB cable
can read it back. Keep the robot where you'd keep a notebook with a password in it.

Rotate a key: change `.env`, run `python3 scripts/chan.py flash-config`. To wipe everything, run
`esptool erase-region 0xE10000 0x10000` (the config partition) or `esptool erase-flash`.

## Network

- The LAN API (`/api/agents`, `/api/ota`) requires `Authorization: Bearer <CHAN_AGENT_TOKEN>` and is
  compared in constant time. `/api/health` is unauthenticated and reveals only the robot's name.
- The LAN API itself is **cleartext HTTP** on your local network (the ESP32 serves no TLS): the
  bearer token, agent messages and firmware images travel unencrypted between your machine and the
  robot. Anyone who can sniff your Wi-Fi can read the token and then post events or push firmware.
  Keep the robot on a network you trust; do not port-forward it to the internet.
- Gemini is reached over TLS (IDF certificate bundle, cross-signed roots accepted).
- Sports, weather and IP-location feeds are fetched over **plain HTTP** on purpose (see
  `docs/architecture.md`, "Memory notes"). Someone on your Wi-Fi could spoof them; nothing secret is
  sent, and the data only drives animations and screens.
- mDNS announces `chan.local` and `_chan._tcp` on your LAN.

## Repository hygiene

`.env`, `config.json`, `backup/` and build output are gitignored; `.env.example` holds placeholders
only. CI runs a secret scan on every push. Never put a real key in a commit message, a log or a
fixture — fixtures under `firmware/tests/fixtures` are sanitized samples.

## Reporting

This is a personal project. If you find a problem, open a private security advisory on GitHub or
email the maintainer listed in the repository profile.
