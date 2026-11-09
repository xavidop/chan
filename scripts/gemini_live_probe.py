#!/usr/bin/env python3
"""Probe the Gemini Live API wire format from a laptop (development aid).

Connects with GEMINI_API_KEY from .env, runs a text turn, a function call and an
audio chunk, prints message *shapes* (never the key, audio data truncated) and
writes trimmed server messages to firmware/tests/fixtures/gemini_*.json so the
firmware's parser is tested against real responses.

Usage: .venv/bin/python scripts/gemini_live_probe.py [--model gemini-3.8-live]
"""

from __future__ import annotations

import argparse
import asyncio
import base64
import json
import math
import pathlib
import struct
import sys

import websockets

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import chan  # noqa: E402

HOST = "wss://generativelanguage.googleapis.com/ws/google.ai.generativelanguage.v1beta.GenerativeService.BidiGenerateContent"
FIXTURES = pathlib.Path(__file__).resolve().parent.parent / "firmware" / "tests" / "fixtures"


def trim(obj, max_data=48):
    """Return a copy with base64 `data` fields truncated (keeps the JSON shape)."""
    if isinstance(obj, dict):
        # keep truncated base64 decodable: cut at a multiple of 4, no ellipsis
        return {k: (v[: (max_data // 4) * 4] if k == "data" and isinstance(v, str) and len(v) > max_data else trim(v, max_data))
                for k, v in obj.items()}
    if isinstance(obj, list):
        return [trim(v, max_data) for v in obj]
    return obj


def shape(obj, depth=0):
    if isinstance(obj, dict):
        return {k: shape(v, depth + 1) for k, v in obj.items()}
    if isinstance(obj, list):
        return [shape(obj[0], depth + 1)] if obj else []
    if isinstance(obj, str):
        return f"str({len(obj)})"
    return type(obj).__name__


def sine_pcm16(seconds=0.5, freq=440.0, rate=16000) -> bytes:
    n = int(seconds * rate)
    return b"".join(struct.pack("<h", int(8000 * math.sin(2 * math.pi * freq * i / rate))) for i in range(n))


async def run(model: str, key: str, language: str, voice: str) -> int:
    saved = {}
    url = f"{HOST}?key={key}"
    async with websockets.connect(url, max_size=16 * 1024 * 1024) as ws:
        setup = {
            "setup": {
                "model": f"models/{model}",
                "generationConfig": {
                    "responseModalities": ["AUDIO"],
                    "speechConfig": {
                        "voiceConfig": {"prebuiltVoiceConfig": {"voiceName": voice}},
                        "languageCode": language,
                    },
                },
                "systemInstruction": {"parts": [{"text": "You are Chan, a tiny cheerful desk robot. Keep answers to one short sentence."}]},
                "tools": [
                    {"googleSearch": {}},
                    {"functionDeclarations": [{
                        "name": "look",
                        "description": "Take a photo with the robot's camera so you can see what is in front of it.",
                        "parameters": {"type": "OBJECT", "properties": {}},
                    }]},
                ],
                "inputAudioTranscription": {},
                "outputAudioTranscription": {},
            }
        }
        await ws.send(json.dumps(setup))
        first = json.loads(await asyncio.wait_for(ws.recv(), 20))
        print("setup ->", json.dumps(shape(first)))
        saved["gemini_setup_complete.json"] = first
        if "setupComplete" not in first:
            print("ERROR: no setupComplete; raw:", json.dumps(trim(first))[:600])
            return 2

        async def collect(label, until_turn_complete=True, timeout=30):
            audio_bytes, msgs = 0, []
            while True:
                raw = await asyncio.wait_for(ws.recv(), timeout)
                msg = json.loads(raw)
                msgs.append(msg)
                sc = msg.get("serverContent", {})
                for p in sc.get("modelTurn", {}).get("parts", []):
                    if "inlineData" in p:
                        audio_bytes += len(base64.b64decode(p["inlineData"]["data"]))
                        mime = p["inlineData"].get("mimeType")
                        if label not in saved:
                            saved[label] = msg
                            print(f"  first audio part mimeType={mime}")
                if "toolCall" in msg:
                    print("  toolCall:", json.dumps(trim(msg)))
                    return msgs, audio_bytes, msg
                if sc.get("interrupted"):
                    print("  interrupted")
                if sc.get("inputTranscription"):
                    print("  inputTranscription:", sc["inputTranscription"].get("text"))
                if sc.get("outputTranscription"):
                    print("  outputTranscription:", sc["outputTranscription"].get("text"))
                if "goAway" in msg:
                    print("  goAway:", msg["goAway"])
                if sc.get("turnComplete") and until_turn_complete:
                    print(f"  turnComplete; {len(msgs)} msgs, {audio_bytes} audio bytes")
                    return msgs, audio_bytes, None

        # 1) text turn → audio
        await ws.send(json.dumps({"clientContent": {"turns": [{"role": "user", "parts": [{"text": "Say hello to Xavi in one short sentence."}]}], "turnComplete": True}}))
        msgs, audio, _ = await collect("gemini_audio_part.json")
        for m in msgs:
            if m.get("serverContent", {}).get("turnComplete"):
                saved["gemini_turn_complete.json"] = m
            if m.get("serverContent", {}).get("outputTranscription"):
                saved.setdefault("gemini_output_transcription.json", m)
        print("text turn: audio bytes =", audio)

        # 2) tool call
        await ws.send(json.dumps({"clientContent": {"turns": [{"role": "user", "parts": [{"text": "What am I holding in my hand right now? Use your camera."}]}], "turnComplete": True}}))
        msgs, audio, tool = await collect("gemini_audio_part_2.json")
        if tool:
            saved["gemini_tool_call.json"] = tool
            calls = tool["toolCall"]["functionCalls"]
            resp = {"toolResponse": {"functionResponses": [{"id": c.get("id"), "name": c["name"], "response": {"ok": True, "note": "image attached"}} for c in calls]}}
            await ws.send(json.dumps(resp))
            # attach an image via realtimeInput (1x1 jpeg)
            jpeg = base64.b64decode("/9j/4AAQSkZJRgABAQEASABIAAD/2wBDAP//////////////////////////////////////////////////////////////////////////////////////wgALCAABAAEBAREA/8QAFBABAAAAAAAAAAAAAAAAAAAAAP/aAAgBAQABPxA=")
            await ws.send(json.dumps({"realtimeInput": {"video": {"mimeType": "image/jpeg", "data": base64.b64encode(jpeg).decode()}}}))
            msgs, audio, _ = await collect("gemini_audio_after_tool.json")
            print("after tool: audio bytes =", audio)
        else:
            print("no toolCall for the camera question (model answered directly)")

        # 3) realtime audio accepted?
        pcm = sine_pcm16()
        await ws.send(json.dumps({"realtimeInput": {"audio": {"mimeType": "audio/pcm;rate=16000", "data": base64.b64encode(pcm).decode()}}}))
        await ws.send(json.dumps({"realtimeInput": {"audioStreamEnd": True}}))
        try:
            raw = await asyncio.wait_for(ws.recv(), 8)
            msg = json.loads(raw)
            print("after audio ->", json.dumps(shape(msg)))
            saved["gemini_after_audio.json"] = msg
        except asyncio.TimeoutError:
            print("after audio -> (no message within 8 s; accepted silently)")

    FIXTURES.mkdir(parents=True, exist_ok=True)
    for name, msg in saved.items():
        (FIXTURES / name).write_text(json.dumps(trim(msg, 96), indent=2) + "\n")
        print("saved fixture", name)
    return 0


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("--model", default="gemini-3.8-live")
    ap.add_argument("--language", default="es-ES")
    ap.add_argument("--voice", default="Aoede")
    args = ap.parse_args()
    env = chan.parse_dotenv(chan.REPO / ".env")
    key = env.get("GEMINI_API_KEY", "")
    if not key:
        print("GEMINI_API_KEY missing in .env", file=sys.stderr)
        return 2
    try:
        return asyncio.run(run(args.model, key, args.language, args.voice))
    except websockets.exceptions.ConnectionClosed as e:
        print("connection closed:", e.code, (e.reason or "")[:300])
        return 3


if __name__ == "__main__":
    raise SystemExit(main())
