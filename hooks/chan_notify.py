#!/usr/bin/env python3
"""Tell Chan what your coding agent is doing. Standard library only, always exits 0.

Wire it as a hook (see hooks/README.md):
  Claude Code : python3 /path/hooks/chan_notify.py --from claude     (hook JSON on stdin)
  Codex       : python3 /path/hooks/chan_notify.py --from codex      (notify JSON as argv[1])
  Gemini CLI  : python3 /path/hooks/chan_notify.py --from gemini     (hook JSON on stdin)
  Copilot CLI : python3 /path/hooks/chan_notify.py --from copilot --event sessionStart  (hook JSON on stdin)
  Anything    : python3 /path/hooks/chan_notify.py --from generic --agent NAME --state STATE [--session ID]

Environment (falls back to the repo's .env next to this script):
  CHAN_HOST          robot host[:port], default chan.local
  CHAN_AGENT_TOKEN   must match CHAN_AGENT_TOKEN in the robot's .env
"""

from __future__ import annotations

import argparse
import json
import os
import pathlib
import sys
import urllib.error
import urllib.request

DEFAULT_HOST = "chan.local"
DEFAULT_TIMEOUT_S = 2.0  # LAN round trips to the robot can take ~1 s in Wi-Fi power save
MESSAGE_MAX = 120

# Claude Code and Gemini CLI hook event names → Chan states
CLAUDE_EVENTS = {
    "Notification": "needs_attention",  # permission prompt / waiting for input
    "UserPromptSubmit": "working",
    "Stop": "done",
    "SessionEnd": "ended",
}
GEMINI_EVENTS = {
    "Notification": "needs_attention",
    "BeforeAgent": "working",
    "AfterAgent": "done",
    "SessionEnd": "ended",
}
CODEX_EVENTS = {
    "agent-turn-complete": "done",
    "approval-requested": "needs_attention",  # if/when Codex exposes approvals via notify
}
# GitHub Copilot CLI hook events (camelCase names → camelCase payload: sessionId, cwd, timestamp).
# The event name is passed with --event because the payload itself does not carry it.
COPILOT_EVENTS = {
    "sessionStart": "working",
    "userPromptSubmitted": "working",
    "notification": "needs_attention",  # permission prompts, elicitation dialogs, idle
    "errorOccurred": "needs_attention",
    "agentStop": "done",
    "sessionEnd": "ended",
}


def _repo_env() -> dict:
    """Fallback settings from the repo's .env next to this script (same machine, same user)."""
    try:
        path = pathlib.Path(__file__).resolve().parent.parent / ".env"
        env = {}
        for raw in path.read_text().splitlines():
            line = raw.strip()
            if not line or line.startswith("#") or "=" not in line:
                continue
            k, v = line.split("=", 1)
            env[k.strip()] = v.strip().strip("\"'")
        return env
    except OSError:
        return {}


def project_from_cwd(cwd: str) -> str:
    cwd = (cwd or "").rstrip("/")
    return pathlib.PurePath(cwd).name if cwd else ""


def _message(text) -> str:
    text = (text or "") if isinstance(text, str) else ""
    text = " ".join(text.split())
    return text[:MESSAGE_MAX]


def build_event(source: str, payload: dict, agent: str | None = None, state: str | None = None,
                session: str | None = None, event: str | None = None) -> dict | None:
    """Map a hook payload to Chan's event, or None when the event is irrelevant."""
    if source == "copilot":
        name = event or payload.get("hookEventName") or payload.get("hook_event_name") or ""
        st = COPILOT_EVENTS.get(name)
        if not st:
            return None
        return {"agent": "copilot", "session": str(payload.get("sessionId") or payload.get("session_id") or "default"),
                "project": project_from_cwd(payload.get("cwd", "")), "state": st,
                "message": _message(payload.get("message") or payload.get("prompt"))}
    if source == "claude":
        st = CLAUDE_EVENTS.get(payload.get("hook_event_name", ""))
        if not st:
            return None
        return {"agent": "claude-code", "session": str(payload.get("session_id") or "default"),
                "project": project_from_cwd(payload.get("cwd", "")), "state": st,
                "message": _message(payload.get("message"))}
    if source == "gemini":
        st = GEMINI_EVENTS.get(payload.get("hook_event_name", ""))
        if not st:
            return None
        return {"agent": "gemini", "session": str(payload.get("session_id") or "default"),
                "project": project_from_cwd(payload.get("cwd", "")), "state": st,
                "message": _message(payload.get("message"))}
    if source == "codex":
        st = CODEX_EVENTS.get(payload.get("type", ""))
        if not st:
            return None
        return {"agent": "codex", "session": str(payload.get("thread-id") or payload.get("thread_id") or "default"),
                "project": project_from_cwd(payload.get("cwd", "")), "state": st,
                "message": _message(payload.get("last-assistant-message") or payload.get("last_assistant_message"))}
    # generic: agent/state from flags or payload
    agent = agent or payload.get("agent")
    state = state or payload.get("state")
    if not agent or not state:
        return None
    return {"agent": str(agent), "session": str(session or payload.get("session") or "default"),
            "project": payload.get("project") or project_from_cwd(payload.get("cwd", "") or os.getcwd()),
            "state": str(state), "message": _message(payload.get("message"))}


def post_event(host: str, token: str, event: dict, timeout_s: float) -> bool:
    url = f"http://{host}/api/agents"
    data = json.dumps(event).encode("utf-8")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/json", "Authorization": f"Bearer {token}"})
    try:
        with urllib.request.urlopen(req, timeout=timeout_s) as resp:
            return 200 <= resp.status < 300
    except (urllib.error.URLError, urllib.error.HTTPError, OSError, ValueError):
        return False


def main(argv: list[str] | None = None, stdin_text: str | None = None) -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--from", dest="source", default="generic", choices=["claude", "codex", "gemini", "copilot", "generic"])
    ap.add_argument("--event", help="hook event name (Copilot CLI payloads do not carry it)")
    ap.add_argument("--agent")
    ap.add_argument("--state")
    ap.add_argument("--session")
    ap.add_argument("--timeout", type=float, default=DEFAULT_TIMEOUT_S)
    ap.add_argument("payload", nargs="?", help="JSON payload (Codex passes it as the first argument)")
    try:
        args = ap.parse_args(argv)
        raw = args.payload
        if raw is None and args.source != "generic":
            raw = stdin_text if stdin_text is not None else (sys.stdin.read() if not sys.stdin.isatty() else "")
        payload = {}
        if raw:
            try:
                payload = json.loads(raw)
            except json.JSONDecodeError:
                payload = {}
        if not isinstance(payload, dict):
            payload = {}
        event = build_event(args.source, payload, args.agent, args.state, args.session, args.event)
        if event is None:
            return 0
        fallback = _repo_env()
        host = os.environ.get("CHAN_HOST") or fallback.get("CHAN_HOST") or DEFAULT_HOST
        token = os.environ.get("CHAN_AGENT_TOKEN") or fallback.get("CHAN_AGENT_TOKEN") or ""
        post_event(host, token, event, args.timeout)
    except Exception:  # never break the agent that called us
        pass
    return 0


if __name__ == "__main__":
    sys.exit(main())
