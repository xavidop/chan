import pathlib
import stat
import sys

sys.path.insert(0, str(pathlib.Path(__file__).resolve().parent))
import install_hooks as ih  # noqa: E402


def _args(tmp_path: pathlib.Path, *extra: str) -> list[str]:
    return ["--claude", str(tmp_path / "claude.json"), "--codex", str(tmp_path / "config.toml"),
            "--copilot-dir", str(tmp_path / "copilot"), *extra]


def _fake_fanout(tmp_path: pathlib.Path, monkeypatch) -> pathlib.Path:
    fanout = tmp_path / "fanout.sh"
    fanout.write_text("#!/bin/sh\n")
    fanout.chmod(0o644)
    monkeypatch.setattr(ih, "FANOUT", fanout)
    return fanout


def test_dry_run_changes_nothing(tmp_path, monkeypatch):
    fanout = _fake_fanout(tmp_path, monkeypatch)
    assert ih.main(_args(tmp_path, "--dry-run")) == 0
    assert stat.S_IMODE(fanout.stat().st_mode) == 0o644
    assert not (tmp_path / "claude.json").exists()
    assert not (tmp_path / "config.toml").exists()
    assert not (tmp_path / "copilot").exists()


def test_real_run_makes_fanout_executable_and_writes_configs(tmp_path, monkeypatch):
    fanout = _fake_fanout(tmp_path, monkeypatch)
    assert ih.main(_args(tmp_path)) == 0
    assert stat.S_IMODE(fanout.stat().st_mode) & stat.S_IXUSR
    assert (tmp_path / "claude.json").exists()
    assert (tmp_path / "config.toml").exists()
    assert (tmp_path / "copilot").is_dir()
