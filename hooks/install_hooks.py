#!/usr/bin/env python3
"""Install Chan's agent hooks for Claude Code, Codex, GitHub Copilot CLI and Antigravity on this machine.

Merges into existing config files (never removes your other hooks) and writes a timestamped
`.bak` copy before touching anything. Standard library only.

Usage: python3 hooks/install_hooks.py [--dry-run] [--claude PATH] [--codex PATH] [--codex-hooks PATH]
                                      [--copilot-dir DIR] [--antigravity PATH]
"""

from __future__ import annotations

import argparse
import json
import pathlib
import re
import shutil
import sys
import time

HERE = pathlib.Path(__file__).resolve().parent
NOTIFY = HERE / "chan_notify.py"
FANOUT = HERE / "codex_notify_fanout.sh"

CLAUDE_EVENTS = ["Notification", "UserPromptSubmit", "Stop", "SessionEnd"]
COPILOT_EVENTS = ["sessionStart", "userPromptSubmitted", "notification", "agentStop", "sessionEnd"]
CODEX_HOOK_EVENTS = ["UserPromptSubmit", "PermissionRequest", "Stop", "SessionEnd"]
ANTIGRAVITY_EVENTS = ["SessionStart", "PreInvocation", "Stop"]
# chan.local can take 5 s to resolve the first time (see chan_notify.py); hooks must outlive that.
HOOK_TIMEOUT_S = 15


def backup(path: pathlib.Path, dry: bool) -> None:
    if path.exists() and not dry:
        shutil.copy2(path, path.with_name(f"{path.name}.bak-chan-{time.strftime('%Y%m%d-%H%M%S')}"))


def install_claude(path: pathlib.Path, dry: bool) -> str:
    cmd = f"python3 {NOTIFY} --from claude"
    data = json.loads(path.read_text()) if path.exists() else {}
    hooks = data.setdefault("hooks", {})
    added = []
    for ev in CLAUDE_EVENTS:
        groups = hooks.setdefault(ev, [])
        if any(h.get("command") == cmd for g in groups for h in g.get("hooks", [])):
            continue
        groups.append({"hooks": [{"type": "command", "command": cmd}]})
        added.append(ev)
    if added and not dry:
        backup(path, dry)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2) + "\n")
    return f"Claude Code ({path}): " + (f"added {', '.join(added)}" if added else "already installed")


def install_codex(path: pathlib.Path, dry: bool) -> str:
    text = path.read_text() if path.exists() else ""
    m = re.search(r"^notify\s*=\s*(\[.*?\])\s*$", text, flags=re.M)
    prev: list[str] = []
    if m:
        try:
            prev = json.loads(m.group(1))  # TOML array of strings is valid JSON
        except json.JSONDecodeError:
            prev = []
    if prev and str(FANOUT) in prev[0]:
        return f"Codex ({path}): already installed"
    new = [str(FANOUT)] + (["--prev", *prev] if prev else [])
    line = "notify = " + json.dumps(new)
    new_text = (text[: m.start()] + line + text[m.end():]) if m else (line + "\n" + text)
    if not dry:
        backup(path, dry)
        path.parent.mkdir(parents=True, exist_ok=True)
        path.write_text(new_text)
    kept = f" (keeping previous notify: {prev[0]})" if prev else ""
    return f"Codex ({path}): notify -> fan-out{kept}"


def _load_json(path: pathlib.Path) -> dict:
    if not path.exists():
        return {}
    try:
        data = json.loads(path.read_text())
    except json.JSONDecodeError:
        return {}
    return data if isinstance(data, dict) else {}


def _write_json(path: pathlib.Path, data: dict, dry: bool) -> None:
    if dry:
        return
    backup(path, dry)
    path.parent.mkdir(parents=True, exist_ok=True)
    path.write_text(json.dumps(data, indent=2) + "\n")


def install_codex_hooks(path: pathlib.Path, dry: bool) -> str:
    """~/.codex/hooks.json: PermissionRequest is the only way to learn that Codex is waiting for you."""
    cmd = f"python3 {NOTIFY} --from codex"
    data = _load_json(path)
    hooks = data.setdefault("hooks", {})
    if not isinstance(hooks, dict):
        hooks = data["hooks"] = {}
    added = []
    for ev in CODEX_HOOK_EVENTS:
        groups = hooks.setdefault(ev, [])
        if any(h.get("command") == cmd for g in groups for h in g.get("hooks", [])):
            continue
        timeout = 3 if ev == "SessionEnd" else HOOK_TIMEOUT_S  # Codex caps SessionEnd hooks at 3 s
        groups.append({"hooks": [{"type": "command", "command": cmd, "timeout": timeout}]})
        added.append(ev)
    if added:
        _write_json(path, data, dry)
    return f"Codex hooks ({path}): " + (f"added {', '.join(added)} — run /hooks in Codex once to trust them"
                                        if added else "already installed")


def install_antigravity(path: pathlib.Path, dry: bool) -> str:
    """~/.gemini/config/hooks.json: one named group; Stop must answer {"decision":"stop"} (chan_notify does)."""
    data = _load_json(path)
    group = {}
    for ev in ANTIGRAVITY_EVENTS:
        group[ev] = [{"type": "command", "command": f"python3 {NOTIFY} --from antigravity --event {ev}",
                      "timeout": HOOK_TIMEOUT_S}]
    if data.get("chan") == group:
        return f"Antigravity ({path}): already installed"
    data["chan"] = group
    _write_json(path, data, dry)
    return f"Antigravity ({path}): written ({len(ANTIGRAVITY_EVENTS)} events; no needs-attention event exists in Antigravity)"


def install_copilot(hooks_dir: pathlib.Path, dry: bool) -> str:
    path = hooks_dir / "chan.json"
    data = {"version": 1, "hooks": {}}
    for ev in COPILOT_EVENTS:
        data["hooks"][ev] = [{"type": "command", "bash": f"python3 {NOTIFY} --from copilot --event {ev}",
                              "timeoutSec": HOOK_TIMEOUT_S}]
    if path.exists() and json.loads(path.read_text()) == data:
        return f"Copilot CLI ({path}): already installed"
    if not dry:
        backup(path, dry)
        hooks_dir.mkdir(parents=True, exist_ok=True)
        path.write_text(json.dumps(data, indent=2) + "\n")
    return f"Copilot CLI ({path}): written ({len(COPILOT_EVENTS)} events)"


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    home = pathlib.Path.home()
    ap.add_argument("--dry-run", action="store_true")
    ap.add_argument("--claude", default=str(home / ".claude/settings.json"))
    ap.add_argument("--codex", default=str(home / ".codex/config.toml"))
    ap.add_argument("--codex-hooks", default=str(home / ".codex/hooks.json"))
    ap.add_argument("--copilot-dir", default=str(home / ".copilot/hooks"))
    ap.add_argument("--antigravity", default=str(home / ".gemini/config/hooks.json"))
    args = ap.parse_args()
    if not NOTIFY.exists():
        print(f"missing {NOTIFY}", file=sys.stderr)
        return 2
    if not args.dry_run:
        FANOUT.chmod(0o755)
    for line in (install_claude(pathlib.Path(args.claude), args.dry_run),
                 install_codex(pathlib.Path(args.codex), args.dry_run),
                 install_codex_hooks(pathlib.Path(args.codex_hooks), args.dry_run),
                 install_copilot(pathlib.Path(args.copilot_dir), args.dry_run),
                 install_antigravity(pathlib.Path(args.antigravity), args.dry_run)):
        print(("[dry-run] " if args.dry_run else "") + line)
    print("Hooks take effect for new agent sessions. Test: python3 hooks/chan_notify.py --from generic --agent test --state needs_attention")
    return 0


if __name__ == "__main__":
    sys.exit(main())
