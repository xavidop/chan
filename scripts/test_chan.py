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


def test_write_backup_sums_creates_checksum_file(tmp_path):
    img = tmp_path / "backup" / "stackchan-stock-16MB.bin"
    img.parent.mkdir()
    img.write_bytes(b"\xab" * 32)
    chan.write_stock_backup_sums(tmp_path)
    sums = (tmp_path / "backup" / "SHA256SUMS").read_text()
    assert sums.split()[0] == hashlib.sha256(b"\xab" * 32).hexdigest()
    assert sums.split()[1].endswith("stackchan-stock-16MB.bin")
    assert chan.verify_stock_backup(tmp_path) == img


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


def test_ota_sends_bearer_and_binary(tmp_path, monkeypatch):
    import http.server
    import socket
    import threading

    received = {}

    class Server(http.server.HTTPServer):
        def server_bind(self):  # avoid getfqdn() reverse-DNS stalls
            socket.socket.bind(self.socket, self.server_address)
            self.server_address = self.socket.getsockname()
            self.server_name, self.server_port = "localhost", self.server_address[1]

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):  # noqa: N802
            n = int(self.headers.get("Content-Length", "0"))
            received.update(path=self.path, auth=self.headers.get("Authorization"), ctype=self.headers.get("Content-Type"),
                            body=self.rfile.read(n))
            self.send_response(200)
            self.end_headers()
            self.wfile.write(b'{"ok":true}')

        def log_message(self, *a):
            pass

    srv = Server(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.handle_request, daemon=True).start()
    image = tmp_path / "stack-chan.bin"
    image.write_bytes(b"\xe9" + b"\x01" * 4095)
    monkeypatch.setattr(chan, "FIRMWARE", tmp_path / "fw")
    (tmp_path / "fw" / "build").mkdir(parents=True)
    (tmp_path / "fw" / "build" / "stack-chan.bin").write_bytes(image.read_bytes())
    monkeypatch.setattr(chan, "REPO", tmp_path)
    (tmp_path / ".env").write_text("CHAN_AGENT_TOKEN=tok-123\n")
    rc = chan.main(["ota", "--host", f"127.0.0.1:{srv.server_port}"])
    srv.server_close()
    assert rc == 0
    assert received["path"] == "/api/ota"
    assert received["auth"] == "Bearer tok-123"
    assert received["ctype"] == "application/octet-stream"
    assert received["body"] == image.read_bytes()


def test_ota_fails_on_401(tmp_path, monkeypatch, capsys):
    import http.server
    import socket
    import threading

    class Server(http.server.HTTPServer):
        def server_bind(self):
            socket.socket.bind(self.socket, self.server_address)
            self.server_address = self.socket.getsockname()
            self.server_name, self.server_port = "localhost", self.server_address[1]

    class Handler(http.server.BaseHTTPRequestHandler):
        def do_POST(self):  # noqa: N802
            self.rfile.read(int(self.headers.get("Content-Length", "0")))
            self.send_response(401)
            self.end_headers()
            self.wfile.write(b'{"error":"bad token"}')

        def log_message(self, *a):
            pass

    srv = Server(("127.0.0.1", 0), Handler)
    threading.Thread(target=srv.handle_request, daemon=True).start()
    monkeypatch.setattr(chan, "FIRMWARE", tmp_path / "fw")
    (tmp_path / "fw" / "build").mkdir(parents=True)
    (tmp_path / "fw" / "build" / "stack-chan.bin").write_bytes(b"\xe9" * 64)
    monkeypatch.setattr(chan, "REPO", tmp_path)
    (tmp_path / ".env").write_text("CHAN_AGENT_TOKEN=wrong\n")
    rc = chan.main(["ota", "--host", f"127.0.0.1:{srv.server_port}"])
    srv.server_close()
    assert rc != 0
    assert "401" in capsys.readouterr().err


def test_ota_refuses_without_image(tmp_path, monkeypatch, capsys):
    monkeypatch.setattr(chan, "FIRMWARE", tmp_path / "fw")
    monkeypatch.setattr(chan, "REPO", tmp_path)
    (tmp_path / ".env").write_text("CHAN_AGENT_TOKEN=t\n")
    assert chan.main(["ota", "--host", "127.0.0.1:9"]) != 0
    assert "build" in capsys.readouterr().err


def test_every_config_key_documented():
    repo = pathlib.Path(__file__).resolve().parent.parent
    doc = (repo / "docs" / "config.md").read_text()

    def paths(obj, prefix=""):
        out = []
        for k, v in obj.items():
            if k.startswith("$"):
                continue
            p = f"{prefix}{k}"
            if isinstance(v, dict) and p != "agents.colors":  # colour map keys are user-chosen agent names
                out += paths(v, p + ".")  # objects are documented per leaf (persona.prompt, …)
            else:
                out.append(p)
        return out

    cfg_keys = paths(json.loads((repo / "config.example.json").read_text()))
    env_keys = [line.split("=", 1)[0] for line in (repo / ".env.example").read_text().splitlines()
                if line.strip() and not line.startswith("#") and "=" in line]
    missing = [k for k in cfg_keys + env_keys if f"`{k}`" not in doc]
    assert not missing, f"undocumented keys: {missing}"


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
