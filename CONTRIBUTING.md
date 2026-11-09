# Contributing

Thanks for helping Chan! A few conventions keep the robot reliable.

## Where things go

- Logic that can be tested on a laptop goes in `firmware/main/chan/core/` — plain C++17, no
  ESP-IDF includes — with a doctest case in `firmware/tests/*_test.cpp`. Fixtures are sanitized
  samples of real API responses in `firmware/tests/fixtures/`.
- Anything touching hardware, LVGL, FreeRTOS or the network goes in `firmware/main/chan/platform/`.
- Changes to the pinned xiaozhi runtime are made in the `firmware/xiaozhi-esp32/` checkout and then
  regenerated into `firmware/patches/xiaozhi-esp32.patch` with `git -C firmware/xiaozhi-esp32 diff`.
  Keep them minimal and commented `// Chan:` so upstream updates stay feasible.
- Stock Stack-chan code under `hal/`, `stackchan/`, `apps/` keeps its original SPDX headers.

## Workflow

1. Write the failing test first (host tests for core, pytest for scripts/hooks).
2. `cmake -S firmware/tests -B build-host && cmake --build build-host && ctest --test-dir build-host`
3. `python3 -m pytest scripts hooks`
4. `python3 scripts/chan.py build` and flash to a robot; use the serial debug console to exercise it.
5. Never commit `.env`, `config.json`, `backup/` or anything with a real key. CI scans for secrets.

## Adding things

- **A sport/league:** soccer leagues only need the ESPN slug in `config.json`. A new sport means a
  `Sport` value, its score lines in `score_line()` and the ESPN path in `sport_path()`.
- **An agent:** hooks are just `POST /api/agents`; add a mapping in `hooks/chan_notify.py` with a test,
  a colour default in `config.cpp`, and a pretty name in `agents_screen.cpp`.
- **A language:** add the BCP-47 and English name in `gemini_messages.cpp` (`kLangs`).
- **A timezone:** add the IANA → POSIX pair in `weather.cpp` (`posix_tz_for`).
- **A tool for the model:** register it in `ChanApp::register_tools()`; keep descriptions short and
  unambiguous (the model confuses "look" with "turn your head").
