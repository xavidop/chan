#!/usr/bin/env python3
"""Synthesize Chan's sound clips and encode them as 16 kHz Opus-in-Ogg.

All clips are generated from scratch here (sine/noise/envelopes), so they carry
no third-party license. Output: firmware/main/assets/sfx/<name>.ogg.
Requires ffmpeg with libopus (brew install ffmpeg).

Usage: python3 scripts/make_sfx.py [--out DIR]
"""

from __future__ import annotations

import argparse
import math
import pathlib
import random
import struct
import subprocess
import tempfile
import wave

RATE = 16000


def env_adsr(n: int, a: float, d: float, s_level: float, r: float) -> list[float]:
    out = []
    a_n, d_n, r_n = int(a * RATE), int(d * RATE), int(r * RATE)
    s_n = max(0, n - a_n - d_n - r_n)
    for i in range(n):
        if i < a_n:
            v = i / max(1, a_n)
        elif i < a_n + d_n:
            v = 1 - (1 - s_level) * (i - a_n) / max(1, d_n)
        elif i < a_n + d_n + s_n:
            v = s_level
        else:
            v = s_level * (1 - (i - a_n - d_n - s_n) / max(1, r_n))
        out.append(max(0.0, v))
    return out


def tone(freq: float, dur: float, vol: float = 0.6, harmonics=(1.0, 0.35, 0.12), glide_to: float | None = None,
         a=0.01, d=0.05, s=0.7, r=0.08) -> list[float]:
    n = int(dur * RATE)
    env = env_adsr(n, a, d, s, r)
    out = []
    phase = 0.0
    for i in range(n):
        f = freq if glide_to is None else freq + (glide_to - freq) * i / n
        phase += 2 * math.pi * f / RATE
        v = sum(h * math.sin(phase * (k + 1)) for k, h in enumerate(harmonics))
        out.append(vol * env[i] * v / sum(harmonics))
    return out


def noise(dur: float, vol: float, a=0.02, d=0.1, s=0.6, r=0.3, lowpass=0.2) -> list[float]:
    n = int(dur * RATE)
    env = env_adsr(n, a, d, s, r)
    out, last = [], 0.0
    rnd = random.Random(42)
    for i in range(n):
        white = rnd.uniform(-1, 1)
        last += lowpass * (white - last)  # one-pole low-pass → softer "crowd" texture
        out.append(vol * env[i] * last)
    return out


def mix(*layers: tuple[list[float], float]) -> list[float]:
    """layers: (samples, start_seconds)"""
    total = max(int(st * RATE) + len(s) for s, st in layers)
    buf = [0.0] * total
    for s, st in layers:
        o = int(st * RATE)
        for i, v in enumerate(s):
            buf[o + i] += v
    peak = max(1e-6, max(abs(v) for v in buf))
    scale = min(1.0, 0.9 / peak)
    return [v * scale for v in buf]


def clips() -> dict[str, list[float]]:
    c4, e4, g4, c5, e5, g5, c6 = 261.63, 329.63, 392.0, 523.25, 659.25, 783.99, 1046.5
    return {
        # Agent needs you: a friendly two-note chime.
        "chime": mix((tone(e5, 0.18, 0.5, a=0.005), 0.0), (tone(g5, 0.32, 0.5, a=0.005, r=0.2), 0.16)),
        # Re-nudge: shorter, softer double blip.
        "nudge": mix((tone(c5, 0.09, 0.35, a=0.004, r=0.04), 0.0), (tone(c5, 0.09, 0.35, a=0.004, r=0.04), 0.14)),
        # Your team scores: rising arpeggio over a crowd swell.
        "cheer": mix(
            (noise(1.6, 0.5, a=0.1, d=0.3, s=0.7, r=0.6), 0.0),
            (tone(c4, 0.14, 0.5), 0.05), (tone(e4, 0.14, 0.5), 0.17), (tone(g4, 0.14, 0.5), 0.29),
            (tone(c5, 0.5, 0.6, r=0.3), 0.41),
        ),
        # Hockey/soccer goal horn: low detuned blast with a crowd tail.
        "goal_horn": mix(
            (tone(146.8, 1.1, 0.7, harmonics=(1.0, 0.8, 0.5, 0.3, 0.2), a=0.02, d=0.1, s=0.9, r=0.25), 0.0),
            (tone(148.5, 1.1, 0.5, harmonics=(1.0, 0.8, 0.5, 0.3), a=0.02, d=0.1, s=0.9, r=0.25), 0.0),
            (noise(1.4, 0.35, a=0.2, d=0.3, s=0.6, r=0.5), 0.2),
        ),
        # Opponent scores: a sulky descending "aww".
        "aww": mix((tone(g4, 0.7, 0.5, harmonics=(1.0, 0.5, 0.2), glide_to=c4, a=0.03, d=0.1, s=0.8, r=0.3), 0.0)),
    }


def write_wav(path: pathlib.Path, samples: list[float]) -> None:
    with wave.open(str(path), "wb") as w:
        w.setnchannels(1)
        w.setsampwidth(2)
        w.setframerate(RATE)
        w.writeframes(b"".join(struct.pack("<h", int(max(-1, min(1, v)) * 32767)) for v in samples))


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--out", default=str(pathlib.Path(__file__).resolve().parent.parent / "firmware/main/assets/sfx"))
    args = ap.parse_args()
    out = pathlib.Path(args.out)
    out.mkdir(parents=True, exist_ok=True)
    with tempfile.TemporaryDirectory() as tmp:
        for name, samples in clips().items():
            wav = pathlib.Path(tmp) / f"{name}.wav"
            write_wav(wav, samples)
            ogg = out / f"{name}.ogg"
            subprocess.run(
                ["ffmpeg", "-y", "-loglevel", "error", "-i", str(wav), "-c:a", "libopus", "-b:a", "24k", "-ar", "16000",
                 "-application", "audio", str(ogg)],
                check=True,
            )
            print(f"{ogg.name}: {len(samples) / RATE:.2f}s, {ogg.stat().st_size} bytes")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
