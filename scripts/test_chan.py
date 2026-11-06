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
