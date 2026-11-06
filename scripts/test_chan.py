"""Tests for scripts/chan.py (run with: python3 -m pytest scripts)."""

import hashlib
import pathlib
import sys

import pytest

sys.path.insert(0, str(pathlib.Path(__file__).parent))
import chan  # noqa: E402


def _write_backup(tmp_path: pathlib.Path, data: bytes, recorded_hash: str | None = None):
    backup = tmp_path / "backup"
    backup.mkdir()
    img = backup / "stackchan-stock-16MB.bin"
    img.write_bytes(data)
    digest = recorded_hash or hashlib.sha256(data).hexdigest()
    (backup / "SHA256SUMS").write_text(f"{digest}  backup/stackchan-stock-16MB.bin\n")
    return img


def test_verify_stock_backup_accepts_matching_checksum(tmp_path):
    img = _write_backup(tmp_path, b"\xff" * 64)
    assert chan.verify_stock_backup(tmp_path) == img


def test_verify_stock_backup_refuses_on_checksum_mismatch(tmp_path):
    _write_backup(tmp_path, b"\xff" * 64, recorded_hash="0" * 64)
    with pytest.raises(chan.ChanError, match="checksum"):
        chan.verify_stock_backup(tmp_path)


def test_verify_stock_backup_refuses_when_missing(tmp_path):
    with pytest.raises(chan.ChanError, match="backup"):
        chan.verify_stock_backup(tmp_path)


# ----------------------------------------------------------------- pack-config

import json
import struct
import zlib


def _write_config_files(tmp_path: pathlib.Path):
    (tmp_path / ".env").write_text(
        "# comment\nWIFI_SSID=home\nWIFI_PASSWORD=pw with spaces\nGEMINI_API_KEY=sk-secret-123\n"
        "CHAN_AGENT_TOKEN=tok\nOPENAI_API_KEY=\nEXTRA_KEY=carried\n"
    )
    (tmp_path / "config.json").write_text(json.dumps({"name": "Testy", "volume": 5}))


def test_pack_roundtrip(tmp_path):
    _write_config_files(tmp_path)
    blob = chan.pack_config(tmp_path / ".env", tmp_path / "config.json")
    assert blob[:4] == b"CHAN"
    length, crc = struct.unpack("<II", blob[4:12])
    payload = blob[12:]
    assert length == len(payload)
    assert crc == zlib.crc32(payload) & 0xFFFFFFFF
    doc = json.loads(payload)
    assert doc["env"]["GEMINI_API_KEY"] == "sk-secret-123"
    assert doc["env"]["WIFI_PASSWORD"] == "pw with spaces"
    assert doc["env"]["EXTRA_KEY"] == "carried"
    assert doc["env"]["OPENAI_API_KEY"] == ""
    assert doc["config"] == {"name": "Testy", "volume": 5}


def test_pack_rejects_missing_env_file(tmp_path):
    (tmp_path / "config.json").write_text("{}")
    with pytest.raises(chan.ChanError, match=r"\.env"):
        chan.pack_config(tmp_path / ".env", tmp_path / "config.json")


def test_pack_rejects_blob_over_partition_size(tmp_path):
    (tmp_path / ".env").write_text("WIFI_SSID=x\n")
    (tmp_path / "config.json").write_text(json.dumps({"big": "x" * 70000}))
    with pytest.raises(chan.ChanError, match="too large"):
        chan.pack_config(tmp_path / ".env", tmp_path / "config.json")


def test_pack_never_prints_secret_values(tmp_path, capsys, monkeypatch):
    _write_config_files(tmp_path)
    out_bin = tmp_path / "out.bin"
    monkeypatch.setattr(chan, "REPO", tmp_path)
    rc = chan.main(["pack-config", "--out", str(out_bin)])
    assert rc == 0
    assert out_bin.is_file()
    captured = capsys.readouterr().out
    assert "GEMINI_API_KEY" in captured
    assert "sk-secret-123" not in captured
    assert "pw with spaces" not in captured
