#!/usr/bin/env python3
"""Create a clean, full-flash SenseCAP distribution from application binaries."""
import argparse
import hashlib
import io
import json
from pathlib import Path
import shutil
import re
import struct
import subprocess
import sys
import tempfile
import zlib

VERSION = "0.1.0-preview"
SIZE = 0x800000
LAYOUT = [("nvs", 1, 2, 0x9000, 0x5000), ("otadata", 1, 0, 0xE000, 0x2000),
          ("app0", 0, 0x10, 0x10000, 0x3E0000), ("meshcore", 0, 0x11, 0x3F0000, 0x250000),
          ("spiffs", 1, 0x82, 0x640000, 0x180000), ("mcfs", 1, 0x82, 0x7C0000, 0x40000)]


def sha(data):
    return hashlib.sha256(data).hexdigest()


def normalize_source_paths(data):
    from esptool.bin_image import ESP32S3FirmwareImage as ESP32FirmwareImage
    from esptool.loader import ESPLoader
    original = ESP32FirmwareImage(io.BytesIO(data))
    assert original.checksum == original.calculate_checksum()
    assert original.stored_digest == original.calc_digest
    patched = bytearray(data)
    count = 0
    for match in re.finditer(rb"[A-Za-z]:[/\\]Users[/\\][^\x00]+", data):
        path = match.group().replace(b"\\", b"/")
        assert path.endswith((b".c", b".h", b".cpp", b".cc", b".hpp")), "Unexpected embedded user path"
        tail = path.find(b"managed_components/")
        if tail < 0:
            tail = path.rfind(b"/") + 1
        assert tail >= 7
        # Keep string length and suffix addresses unchanged for merged C literals.
        prefix = b"build/" + b"_" * (tail - 7) + b"/"
        patched[match.start():match.start() + tail] = prefix
        count += 1
    if count:
        offset, checksum = 24, ESPLoader.ESP_CHECKSUM_MAGIC
        for _ in range(patched[1]):
            _, length = struct.unpack_from("<II", patched, offset)
            offset += 8
            checksum = ESPLoader.checksum(patched[offset:offset + length], checksum)
            offset += length
        assert original.data_length - 1 == offset + 15 - offset % 16
        patched[original.data_length - 1] = checksum
        patched[original.data_length:original.data_length + 32] = hashlib.sha256(patched[:original.data_length]).digest()
    verified = ESP32FirmwareImage(io.BytesIO(patched))
    assert verified.checksum == verified.calculate_checksum()
    assert verified.stored_digest == verified.calc_digest
    assert len(patched) == len(data)
    return bytes(patched), count


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    for name in ("bootloader", "partitions", "meshtastic", "meshcore", "mklittlefs", "mkspiffs", "rp2040", "output"):
        parser.add_argument("--" + name, type=Path, required=True)
    args = parser.parse_args()
    output = args.output.resolve()
    output.mkdir(parents=True, exist_ok=True)
    table = args.partitions.read_bytes()
    for index, expected in enumerate(LAYOUT):
        magic, kind, subtype, offset, length, name, _ = struct.unpack_from("<HBBII16sI", table, index * 32)
        assert magic == 0x50AA
        assert (name.rstrip(b"\0").decode(), kind, subtype, offset, length) == expected
    assert args.meshtastic.stat().st_size <= 0x3E0000
    assert args.meshcore.stat().st_size <= 0x250000
    for app in (args.meshtastic, args.meshcore):
        assert app.read_bytes()[0] == 0xE9
        assert struct.unpack_from("<I", app.read_bytes(), 32)[0] == 0xABCD5432
    shutil.copyfile(args.bootloader, output / "bootloader.bin")
    shutil.copyfile(args.partitions, output / "partitions.bin")
    mt, mt_paths = normalize_source_paths(args.meshtastic.read_bytes())
    mc, mc_paths = normalize_source_paths(args.meshcore.read_bytes())
    assert b"cairn_setup" in mc, "Use the sensecap-meshcore-public build for redistribution"
    (output / "meshtastic.bin").write_bytes(mt)
    (output / "meshcore.bin").write_bytes(mc)
    shutil.copyfile(args.rp2040, output / "indicator-rp2040-v2.1.uf2")
    ota = bytearray(b"\xff" * 8192)
    for offset, sequence in ((0, 3), (4096, 2)):
        struct.pack_into("<I20sII", ota, offset, sequence, b"\xff" * 20, 2,
                         zlib.crc32(struct.pack("<I", sequence), 0xFFFFFFFF))
    (output / "otadata.bin").write_bytes(ota)
    with tempfile.TemporaryDirectory(prefix="cairn-clean-") as scratch:
        scratch = Path(scratch)
        empty, core = scratch / "empty", scratch / "meshcore"
        empty.mkdir()
        core.mkdir()
        (core / "meshcore.profile").write_bytes(b"1\n")
        subprocess.run([args.mklittlefs.as_posix(), "-c", empty.as_posix(), "-b", "4096", "-p", "256", "-s", "1572864",
                        (output / "meshtastic-empty.littlefs.bin").as_posix()], check=True)
        subprocess.run([args.mkspiffs.as_posix(), "-c", core.as_posix(), "-b", "4096", "-p", "256", "-s", "262144",
                        (output / "meshcore-clean.spiffs.bin").as_posix()], check=True)
        listing = subprocess.check_output([args.mkspiffs.as_posix(), "-l", "-b", "4096", "-p", "256", "-s", "262144",
                                           (output / "meshcore-clean.spiffs.bin").as_posix()], text=True)
        assert listing.strip() == "2\t/meshcore.profile", listing
        unpack = scratch / "verify"
        unpack.mkdir()
        subprocess.run([args.mkspiffs.as_posix(), "-u", unpack.as_posix(), "-b", "4096", "-p", "256", "-s", "262144",
                        (output / "meshcore-clean.spiffs.bin").as_posix()], check=True)
        assert [p.relative_to(unpack).as_posix() for p in unpack.rglob("*") if p.is_file()] == ["meshcore.profile"]
        assert (unpack / "meshcore.profile").read_bytes() == b"1\n"
        listing = subprocess.check_output([args.mklittlefs.as_posix(), "-l", "-b", "4096", "-p", "256", "-s", "1572864",
                                           (output / "meshtastic-empty.littlefs.bin").as_posix()], text=True)
        assert not [line for line in listing.splitlines() if line.strip() and not line.startswith("Creation time:")], listing
    factory = output / f"sensecap-dual-{VERSION}.factory.bin"
    parts = [(0, "bootloader.bin"), (0x8000, "partitions.bin"), (0xE000, "otadata.bin"),
             (0x10000, "meshtastic.bin"), (0x3F0000, "meshcore.bin"),
             (0x640000, "meshtastic-empty.littlefs.bin"), (0x7C0000, "meshcore-clean.spiffs.bin")]
    command = [sys.executable, "-m", "esptool", "--chip", "esp32s3", "merge-bin", "--flash-mode", "dio",
               "--flash-freq", "80m", "--flash-size", "8MB", "--pad-to-size", "8MB", "-o", str(factory)]
    for offset, name in parts:
        command += [hex(offset), str(output / name)]
    subprocess.run(command, check=True)
    image = factory.read_bytes()
    assert len(image) == SIZE
    assert image[0x9000:0xE000] == b"\xff" * 0x5000
    for offset, name in parts[1:]:
        data = (output / name).read_bytes()
        assert image[offset:offset + len(data)] == data, name
    # esptool can update the bootloader's flash parameters and its digest.
    (output / "bootloader.bin").write_bytes(image[:args.bootloader.stat().st_size])
    files = {p.name: {"bytes": p.stat().st_size, "sha256": sha(p.read_bytes())}
             for p in sorted(output.iterdir()) if p.is_file() and p.suffix in (".bin", ".uf2")}
    provenance = dict(version=VERSION, board="SenseCAP Indicator for Meshtastic (ESP32-S3, 8 MB flash, 8 MB PSRAM, SX1262)",
                      defaultProtocol="Meshtastic", meshcoreTransmit="Disabled until radio settings are saved on device",
                      userConfiguration="Blank NVS; empty Meshtastic volume; MeshCore profile marker only",
                      diagnosticPathsNormalized=mt_paths + mc_paths,
                      meshtasticBase="08cd97e", meshcoreBase="81dbae592ffad40b4212bc0b37e82b38da08f4bc",
                      rp2040Source="https://github.com/meshtastic/indicator_rp2040/releases/tag/v2.1", files=files)
    (output / "release.json").write_text(json.dumps(provenance, indent=2) + "\n", encoding="utf-8")
    (output / "SHA256SUMS.txt").write_text("".join(f"{info['sha256']}  {name}\n" for name, info in files.items()), encoding="utf-8")
    manifest = dict(name="CAIRN Dual + HamView — SenseCAP Indicator", version=VERSION, new_install_prompt_erase=True,
                    builds=[dict(chipFamily="ESP32-S3", parts=[dict(path="firmware/" + factory.name, offset=0)])])
    (output.parent / "manifest.json").write_text(json.dumps(manifest, indent=2) + "\n", encoding="utf-8")
    print("Clean package verified:", factory, sha(image))


if __name__ == "__main__":
    main()
