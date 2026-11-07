#!/usr/bin/env python3
"""Chan developer CLI: build, flash, monitor, restore the stock firmware.

Usage:
  python3 scripts/chan.py setup                 # print how to install/activate ESP-IDF
  python3 scripts/chan.py build                 # idf.py build
  python3 scripts/chan.py flash [--port PORT]   # idf.py flash
  python3 scripts/chan.py monitor [--port PORT] # idf.py monitor
  python3 scripts/chan.py backup-stock [--port PORT]   # read the stock firmware into backup/ (do this first)
  python3 scripts/chan.py restore-stock [--port PORT]  # write backup/stackchan-stock-16MB.bin
  python3 scripts/chan.py pack-config [--out FILE]     # .env + config.json -> chan-config.bin
  python3 scripts/chan.py flash-config [--port PORT]   # pack and write it to the `chancfg` partition
  python3 scripts/chan.py ota [--host chan.local]      # push firmware/build/stack-chan.bin over Wi-Fi

Everything shells out to the ESP-IDF tools, so run `. ~/esp/esp-idf/export.sh`
first (see `setup`). No secrets are ever printed by this script.
"""

from __future__ import annotations

import argparse
import hashlib
import json
import os
import pathlib
import shutil
import struct
import subprocess
import sys
import zlib

REPO = pathlib.Path(__file__).resolve().parent.parent
FIRMWARE = REPO / "firmware"
STOCK_IMAGE = "stackchan-stock-16MB.bin"
IDF_VERSION = "v6.1"
DEFAULT_IDF_PATH = pathlib.Path.home() / "esp" / "esp-idf"

# `chancfg` partition in firmware/partitions.csv — keep these three in sync.
CONFIG_PARTITION_OFFSET = 0xE10000
CONFIG_PARTITION_SIZE = 0x10000
CONFIG_MAGIC = b"CHAN"
CONFIG_HEADER = struct.Struct("<4sII")  # magic, payload length, crc32(payload)
CONFIG_MAX_PAYLOAD = CONFIG_PARTITION_SIZE - CONFIG_HEADER.size


class ChanError(Exception):
    """A user-facing error; printed without a traceback."""


# --------------------------------------------------------------------------- helpers


def _run(cmd: list[str], cwd: pathlib.Path | None = None) -> None:
    print("+", " ".join(cmd), flush=True)
    subprocess.run(cmd, cwd=cwd, check=True)


def _require_idf() -> None:
    if shutil.which("idf.py") is None:
        raise ChanError(
            "idf.py not found. Run `python3 scripts/chan.py setup` for instructions, "
            f"then `. {DEFAULT_IDF_PATH}/export.sh` in this shell."
        )


def _port_args(port: str | None) -> list[str]:
    return ["-p", port] if port else []


def write_stock_backup_sums(repo: pathlib.Path = REPO) -> pathlib.Path:
    """Write backup/SHA256SUMS for the stock image (what `restore-stock` verifies against)."""
    image = repo / "backup" / STOCK_IMAGE
    if not image.is_file():
        raise ChanError(f"{image} not found")
    digest = hashlib.sha256(image.read_bytes()).hexdigest()
    sums = image.with_name("SHA256SUMS")
    sums.write_text(f"{digest}  backup/{STOCK_IMAGE}\n")
    return sums


def verify_stock_backup(repo: pathlib.Path = REPO) -> pathlib.Path:
    """Return the stock image path after checking it against backup/SHA256SUMS.

    Raises ChanError if the backup or its checksum file is missing, or if the
    recorded checksum does not match the file on disk.
    """
    backup_dir = repo / "backup"
    image = backup_dir / STOCK_IMAGE
    sums = backup_dir / "SHA256SUMS"
    if not image.is_file() or not sums.is_file():
        raise ChanError(
            f"stock backup not found: expected {image} and {sums}. "
            "Read it from the device with esptool before replacing the firmware."
        )
    recorded = None
    for line in sums.read_text().splitlines():
        parts = line.split()
        if len(parts) >= 2 and parts[1].endswith(STOCK_IMAGE):
            recorded = parts[0].lower()
    if recorded is None:
        raise ChanError(f"{sums} has no entry for {STOCK_IMAGE}")
    actual = hashlib.sha256(image.read_bytes()).hexdigest()
    if actual != recorded:
        raise ChanError(
            f"checksum mismatch for {image}: recorded {recorded[:12]}…, actual {actual[:12]}…. "
            "Refusing to flash a corrupted backup."
        )
    return image


def parse_dotenv(path: pathlib.Path) -> dict[str, str]:
    """Minimal .env reader: KEY=VALUE per line, `#` comments, no interpolation.

    Values keep inner spaces; surrounding single/double quotes are stripped.
    Unknown keys are carried through so future features can read them.
    """
    if not path.is_file():
        raise ChanError(f"{path} not found. Copy .env.example to .env and fill it in.")
    env: dict[str, str] = {}
    for raw in path.read_text().splitlines():
        line = raw.strip()
        if not line or line.startswith("#") or "=" not in line:
            continue
        key, value = line.split("=", 1)
        key = key.strip()
        if key.startswith("export "):
            key = key[len("export "):].strip()
        value = value.strip()
        if len(value) >= 2 and value[0] == value[-1] and value[0] in "\"'":
            value = value[1:-1]
        env[key] = value
    return env


def pack_config(env_path: pathlib.Path, config_path: pathlib.Path) -> bytes:
    """Build the `chancfg` partition image from .env and config.json.

    Layout: b"CHAN" | u32 payload length (LE) | u32 crc32(payload) (LE) | payload,
    where payload is the compact JSON {"env": {...}, "config": {...}}.
    """
    env = parse_dotenv(env_path)
    if not config_path.is_file():
        raise ChanError(f"{config_path} not found. Copy config.example.json to config.json.")
    try:
        config = json.loads(config_path.read_text())
    except json.JSONDecodeError as e:
        raise ChanError(f"{config_path} is not valid JSON: {e}") from e
    if not isinstance(config, dict):
        raise ChanError(f"{config_path} must contain a JSON object at the top level")
    config.pop("$comment", None)
    payload = json.dumps({"env": env, "config": config}, ensure_ascii=False, separators=(",", ":")).encode("utf-8")
    if len(payload) > CONFIG_MAX_PAYLOAD:
        raise ChanError(
            f"config blob too large: {len(payload)} bytes, partition holds {CONFIG_MAX_PAYLOAD}. "
            "Trim config.json (fewer teams, shorter persona)."
        )
    header = CONFIG_HEADER.pack(CONFIG_MAGIC, len(payload), zlib.crc32(payload) & 0xFFFFFFFF)
    return header + payload


def _default_config_out() -> pathlib.Path:
    return FIRMWARE / "build" / "chan-config.bin"


def _write_config_image(out: pathlib.Path) -> pathlib.Path:
    blob = pack_config(REPO / ".env", REPO / "config.json")
    out.parent.mkdir(parents=True, exist_ok=True)
    out.write_bytes(blob)
    env_keys = sorted(parse_dotenv(REPO / ".env").keys())
    # Only key names are printed — never values.
    print(f"Packed {len(blob)} bytes → {out}")
    print(f"  env keys: {', '.join(env_keys)}")
    return out


# --------------------------------------------------------------------------- commands


def cmd_setup(_: argparse.Namespace) -> None:
    print(
        f"""Chan needs ESP-IDF {IDF_VERSION} (ESP32-S3 target).

Install once:
  brew install cmake ninja dfu-util
  mkdir -p ~/esp && cd ~/esp
  git clone -b {IDF_VERSION} --recursive https://github.com/espressif/esp-idf.git
  ~/esp/esp-idf/install.sh esp32s3

Activate in every new shell before using this script:
  . ~/esp/esp-idf/export.sh

Then, from the repo root:
  (cd firmware && python3 fetch_repos.py)   # pinned upstream deps + patches
  python3 scripts/chan.py build
  python3 scripts/chan.py flash --port /dev/cu.usbmodem101
"""
    )


def cmd_build(_: argparse.Namespace) -> None:
    _require_idf()
    _run(["idf.py", "build"], cwd=FIRMWARE)


def cmd_flash(args: argparse.Namespace) -> None:
    _require_idf()
    _run(["idf.py", *_port_args(args.port), "flash"], cwd=FIRMWARE)


def cmd_monitor(args: argparse.Namespace) -> None:
    _require_idf()
    _run(["idf.py", *_port_args(args.port), "monitor"], cwd=FIRMWARE)


def cmd_backup_stock(args: argparse.Namespace) -> None:
    """Read the whole 16 MB flash into backup/ and record its checksum — do this before the first flash."""
    esptool = shutil.which("esptool") or shutil.which("esptool.py")
    if esptool is None:
        raise ChanError("esptool not found; activate ESP-IDF (`. ~/esp/esp-idf/export.sh`) or `pip install esptool`.")
    image = REPO / "backup" / STOCK_IMAGE
    if image.exists() and not args.force:
        raise ChanError(f"{image} already exists; pass --force to overwrite it.")
    image.parent.mkdir(parents=True, exist_ok=True)
    print("Reading 16 MB of flash (a few minutes)…")
    _run([esptool, *_port_args(args.port), "--baud", "921600", "read-flash", "0", "0x1000000", str(image)])
    sums = write_stock_backup_sums()
    print(f"Saved {image} and {sums}. `restore-stock` can now put the robot back exactly as it shipped.")


def cmd_restore_stock(args: argparse.Namespace) -> None:
    image = verify_stock_backup()
    esptool = shutil.which("esptool") or shutil.which("esptool.py")
    if esptool is None:
        raise ChanError("esptool not found; activate ESP-IDF (`. ~/esp/esp-idf/export.sh`) or `pip install esptool`.")
    print(f"Restoring stock firmware from {image} (16 MB, this takes a few minutes)…")
    _run([esptool, *_port_args(args.port), "--baud", "921600", "write-flash", "0x0", str(image)])
    print("Done. The robot should reboot into the stock Stack-chan firmware.")


def cmd_ota(args: argparse.Namespace) -> None:
    """Push firmware/build/stack-chan.bin to the robot over Wi-Fi (POST /api/ota, bearer token)."""
    import urllib.error
    import urllib.request

    image = FIRMWARE / "build" / "stack-chan.bin"
    if not image.is_file():
        raise ChanError(f"{image} not found; run `python3 scripts/chan.py build` first.")
    token = parse_dotenv(REPO / ".env").get("CHAN_AGENT_TOKEN", "")
    if not token:
        raise ChanError("CHAN_AGENT_TOKEN missing in .env")
    data = image.read_bytes()
    url = f"http://{args.host}/api/ota"
    print(f"Uploading {len(data) / 1024:.0f} KB to {url} …")
    req = urllib.request.Request(url, data=data, method="POST",
                                 headers={"Content-Type": "application/octet-stream", "Authorization": f"Bearer {token}",
                                          "Content-Length": str(len(data))})
    try:
        with urllib.request.urlopen(req, timeout=args.timeout) as resp:
            body = resp.read().decode("utf-8", "replace")
            print(f"Robot accepted the image ({resp.status}): {body.strip()}")
            print("It is rebooting into the new firmware now; if it fails to boot, the bootloader rolls back.")
    except urllib.error.HTTPError as e:
        detail = e.read().decode("utf-8", "replace").strip()
        raise ChanError(f"robot refused the upload: HTTP {e.code} {detail}") from e
    except (urllib.error.URLError, OSError) as e:
        raise ChanError(f"could not reach the robot at {args.host}: {e}") from e


def cmd_pack_config(args: argparse.Namespace) -> None:
    _write_config_image(pathlib.Path(args.out) if args.out else _default_config_out())


def cmd_flash_config(args: argparse.Namespace) -> None:
    out = _write_config_image(_default_config_out())
    esptool = shutil.which("esptool") or shutil.which("esptool.py")
    if esptool is None:
        raise ChanError("esptool not found; activate ESP-IDF (`. ~/esp/esp-idf/export.sh`) or `pip install esptool`.")
    _run([esptool, *_port_args(args.port), "--baud", "921600", "write-flash", hex(CONFIG_PARTITION_OFFSET), str(out)])
    print("Config written. The robot reboots and picks it up on the next start.")


def main(argv: list[str] | None = None) -> int:
    parser = argparse.ArgumentParser(prog="chan.py", description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)

    sub.add_parser("setup", help="print ESP-IDF install/activation instructions").set_defaults(fn=cmd_setup)
    sub.add_parser("build", help="build the firmware").set_defaults(fn=cmd_build)
    p = sub.add_parser("backup-stock", help="read the stock firmware into backup/ with a checksum (do this first)")
    p.add_argument("--port", "-p", default=os.environ.get("CHAN_PORT"), help="serial port (or $CHAN_PORT)")
    p.add_argument("--force", action="store_true", help="overwrite an existing backup")
    p.set_defaults(fn=cmd_backup_stock)
    for name, fn, help_ in (
        ("flash", cmd_flash, "flash bootloader, partitions and app"),
        ("monitor", cmd_monitor, "open the serial monitor"),
        ("restore-stock", cmd_restore_stock, "write the stock firmware backup back to the device"),
        ("flash-config", cmd_flash_config, "pack .env + config.json and write them to the chancfg partition"),
    ):
        p = sub.add_parser(name, help=help_)
        p.add_argument("--port", "-p", default=os.environ.get("CHAN_PORT"), help="serial port (or $CHAN_PORT)")
        p.set_defaults(fn=fn)
    p = sub.add_parser("ota", help="push the built firmware to the robot over Wi-Fi")
    p.add_argument("--host", default=os.environ.get("CHAN_HOST", "chan.local"), help="robot host[:port] (or $CHAN_HOST)")
    p.add_argument("--timeout", type=float, default=180.0)
    p.set_defaults(fn=cmd_ota)
    p = sub.add_parser("pack-config", help="pack .env + config.json into a chancfg partition image")
    p.add_argument("--out", help="output file (default firmware/build/chan-config.bin)")
    p.set_defaults(fn=cmd_pack_config)

    args = parser.parse_args(argv)
    try:
        args.fn(args)
    except ChanError as e:
        print(f"error: {e}", file=sys.stderr)
        return 2
    except subprocess.CalledProcessError as e:
        print(f"error: command failed with exit code {e.returncode}", file=sys.stderr)
        return e.returncode
    return 0


if __name__ == "__main__":
    sys.exit(main())
