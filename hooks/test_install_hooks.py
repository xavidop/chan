"""Tests for hooks/install_hooks.py (run with: python3 -m pytest hooks)."""

import json
import pathlib
import sys

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import install_hooks as ih  # noqa: E402


def test_codex_hooks_json_is_created_with_the_four_events(tmp_path):
    path = tmp_path / "hooks.json"
    msg = ih.install_codex_hooks(path, dry=False)
    data = json.loads(path.read_text())
    events = data["hooks"]
    assert set(events) == {"UserPromptSubmit", "PermissionRequest", "Stop", "SessionEnd"}
    for ev, groups in events.items():
        cmds = [h["command"] for g in groups for h in g["hooks"]]
        assert any("chan_notify.py --from codex" in c for c in cmds), ev
    # SessionEnd hooks are capped at 3 s by Codex
    assert all(h["timeout"] <= 3 for g in events["SessionEnd"] for h in g["hooks"])
    assert "written" in msg or "added" in msg
    assert ih.install_codex_hooks(path, dry=False).endswith("already installed")


def test_codex_hooks_json_merge_keeps_other_hooks(tmp_path):
    path = tmp_path / "hooks.json"
    path.write_text(json.dumps({"hooks": {"PostToolUse": [{"matcher": "Bash", "hooks": [{"type": "command", "command": "lint.sh"}]}],
                                          "Stop": [{"hooks": [{"type": "command", "command": "other.sh"}]}]}}))
    ih.install_codex_hooks(path, dry=False)
    data = json.loads(path.read_text())
    assert data["hooks"]["PostToolUse"][0]["hooks"][0]["command"] == "lint.sh"
    stop_cmds = [h["command"] for g in data["hooks"]["Stop"] for h in g["hooks"]]
    assert "other.sh" in stop_cmds and any("chan_notify" in c for c in stop_cmds)


def test_antigravity_hooks_json_gets_a_chan_group(tmp_path):
    path = tmp_path / "hooks.json"
    path.write_text(json.dumps({"my-linter": {"PostToolUse": [{"matcher": "run_command", "hooks": [{"command": "lint.sh"}]}]}}))
    ih.install_antigravity(path, dry=False)
    data = json.loads(path.read_text())
    assert "my-linter" in data
    chan = data["chan"]
    assert set(chan) == {"SessionStart", "PreInvocation", "Stop"}
    for ev, handlers in chan.items():
        assert handlers[0]["type"] == "command"
        assert f"chan_notify.py --from antigravity --event {ev}" in handlers[0]["command"]
        assert handlers[0]["timeout"] >= 10
    assert ih.install_antigravity(path, dry=False).endswith("already installed")


def test_copilot_hook_timeout_survives_a_slow_mdns_lookup(tmp_path):
    ih.install_copilot(tmp_path, dry=False)
    data = json.loads((tmp_path / "chan.json").read_text())
    assert all(h["timeoutSec"] >= 15 for hs in data["hooks"].values() for h in hs)


def test_dry_run_writes_nothing(tmp_path):
    ih.install_codex_hooks(tmp_path / "a.json", dry=True)
    ih.install_antigravity(tmp_path / "b.json", dry=True)
    ih.install_copilot(tmp_path / "cp", dry=True)
    assert list(tmp_path.iterdir()) == []
