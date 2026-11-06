# Sound effects

| File | Used for | Source |
|---|---|---|
| `chime.ogg` | an agent needs you | synthesized by `scripts/make_sfx.py` (MIT, this repo) |
| `nudge.ogg` | agent re-nudge | synthesized by `scripts/make_sfx.py` |
| `cheer.ogg` | your team scores (NFL/NBA/MLB) | synthesized by `scripts/make_sfx.py` |
| `goal_horn.ogg` | your team scores (soccer/NHL) | synthesized by `scripts/make_sfx.py` |
| `aww.ogg` | the other team scores | synthesized by `scripts/make_sfx.py` |
| `new_notification.ogg`, `camera_shutter.ogg` | stock | from m5stack/StackChan (MIT) |

All clips are 16 kHz mono Opus in Ogg (`ffmpeg -c:a libopus -b:a 24k -ar 16000`), the
format the audio service plays directly. Regenerate with `python3 scripts/make_sfx.py`.
