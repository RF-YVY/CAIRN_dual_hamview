#!/usr/bin/env python3
"""Verify the published factory image, clean settings area and release hashes."""
import hashlib
import json
from pathlib import Path
import struct
import zlib

ROOT = Path(__file__).resolve().parents[1]
FILES = ROOT / "web/firmware"
release = json.loads((FILES / "release.json").read_text(encoding="utf-8"))
for name, info in release["files"].items():
    assert Path(name).name == name
    data = (FILES / name).read_bytes()
    assert len(data) == info["bytes"], name
    assert hashlib.sha256(data).hexdigest() == info["sha256"], name
image = (FILES / f"sensecap-dual-{release['version']}.factory.bin").read_bytes()
assert len(image) == 0x800000
assert image[0x9000:0xE000] == b"\xff" * 0x5000, "User settings area must be erased"
parts = [(0, "bootloader.bin"), (0x8000, "partitions.bin"), (0xE000, "otadata.bin"),
         (0x10000, "meshtastic.bin"), (0x3F0000, "meshcore.bin"),
         (0x640000, "meshtastic-empty.littlefs.bin"), (0x7C0000, "meshcore-clean.spiffs.bin")]
for offset, name in parts:
    data = (FILES / name).read_bytes()
    assert image[offset:offset + len(data)] == data, name
for offset, sequence in ((0xE000, 3), (0xF000, 2)):
    seq, label, state, crc = struct.unpack_from("<I20sII", image, offset)
    assert seq == sequence and label == b"\xff" * 20 and state == 2
    assert crc == zlib.crc32(struct.pack("<I", seq), 0xFFFFFFFF)
assert len((FILES / "meshtastic.bin").read_bytes()) <= 0x3E0000
assert len((FILES / "meshcore.bin").read_bytes()) <= 0x250000
assert b"cairn_setup" in (FILES / "meshcore.bin").read_bytes(), "Public radio setup gate missing"
prefs = ROOT / "firmware/userPrefs.jsonc"
values = json.loads("\n".join(line for line in prefs.read_text().splitlines() if not line.lstrip().startswith("//")))
assert set(values) <= {"USERPREFS_RINGTONE_RTTTL", "USERPREFS_TZ_STRING"}, "Unexpected prefilled factory preference"
assert values.get("USERPREFS_TZ_STRING", "").strip() in ("", "tzplaceholder", "UTC0")
manifest = json.loads((ROOT / "web/manifest.json").read_text(encoding="utf-8"))
assert manifest["builds"] == [{"chipFamily": "ESP32-S3", "parts": [{"path": f"firmware/sensecap-dual-{release['version']}.factory.bin", "offset": 0}]}]
print("PASS: release hashes, flash bounds, merged parts, blank user settings, public radio setup and web manifest")
