import argparse
import hashlib
import json
from pathlib import Path
import struct
import time
import zlib

import serial


class DeviceError(RuntimeError):
    pass


class CairnClient:
    def __init__(self, port="COM28", baud=115200):
        self.serial = serial.Serial(port=None, baudrate=baud, timeout=0.2)
        self.serial.dtr = self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.buffer = bytearray()
        self.sequence = 0
        self.initial_baud = baud

    def __enter__(self):
        return self

    def __exit__(self, *_):
        if self.serial.baudrate != self.initial_baud:
            try:
                self.change_baud(self.initial_baud)
            except Exception:
                pass
        self.serial.close()

    def request(self, op, path="", body=b"", timeout=10):
        self.sequence = (self.sequence + 1) & 0xFFFFFFFF or 1
        path = path.encode("utf-8")
        payload = bytes([op]) + struct.pack("<IH", self.sequence, len(path)) + path + body
        payload += struct.pack("<I", zlib.crc32(payload))
        if len(payload) > 4608:
            raise ValueError("Request exceeds the device frame limit")
        self.serial.write(b"\x94\xc3" + struct.pack(">H", len(payload)) + payload)
        deadline = time.monotonic() + timeout
        while time.monotonic() < deadline:
            self.buffer.extend(self.serial.read(self.serial.in_waiting or 1))
            while len(self.buffer) >= 4:
                if self.buffer[:2] != b"\x94\xc3":
                    del self.buffer[0]
                    continue
                size = struct.unpack_from(">H", self.buffer, 2)[0]
                if not 10 <= size <= 4608:
                    del self.buffer[0]
                    continue
                if len(self.buffer) < size + 4:
                    break
                frame = bytes(self.buffer[4:size + 4])
                del self.buffer[:size + 4]
                if zlib.crc32(frame[:-4]) != struct.unpack_from("<I", frame, size - 4)[0]:
                    continue
                if frame[0] != (op | 0x80) or struct.unpack_from("<I", frame, 1)[0] != self.sequence:
                    continue
                if frame[5]:
                    raise DeviceError(f"Device refused operation {op}")
                return frame[6:-4]
        raise TimeoutError(f"No response to operation {op}")

    def info(self):
        version, mac, capacity = struct.unpack("<HQH", self.request(0))
        return dict(protocol=version, mac=mac.to_bytes(8, "little")[:6].hex(":"), capacity=capacity)

    def change_baud(self, baud):
        self.request(7, body=struct.pack("<I", baud))
        time.sleep(0.05)
        self.serial.baudrate = baud
        self.info()

    def sd_info(self):
        present, card, fat, flags, total, used, free = struct.unpack("<BBBBQQQ", self.request(1))
        return dict(present=bool(present), card_type=card, fat_type=fat, busy=bool(flags & 2),
                    stats_valid=bool(flags & 1), total=total, used=used, free=free)

    def listdir(self, path="/"):
        offset = 0
        entries = []
        while True:
            data = self.request(4, path, struct.pack("<I", offset))
            total, count = struct.unpack_from("<IH", data)
            cursor = 6
            for _ in range(count):
                size = struct.unpack_from("<H", data, cursor)[0]
                cursor += 2
                entries.append(data[cursor:cursor + size].decode("utf-8"))
                cursor += size
            if offset + count >= total:
                return entries
            if not count:
                raise DeviceError("Directory listing stopped before its final page")
            offset += count

    def read(self, path, offset, count=4096):
        for attempt in range(3):
            try:
                data = self.request(2, path, struct.pack("<IH", offset, count))
                break
            except TimeoutError:
                if attempt == 2:
                    raise
        total, received = struct.unpack_from("<IH", data)
        chunk = data[6:]
        if len(chunk) != received:
            raise DeviceError("Short file-read response")
        return chunk, total

    def get(self, path, target):
        offset = 0
        with Path(target).open("wb") as output:
            while True:
                chunk, size = self.read(path, offset)
                output.write(chunk)
                offset += len(chunk)
                if offset == size:
                    return
                if not chunk or offset > size:
                    raise DeviceError("Invalid file length")

    def file_hash(self, path):
        data = self.request(8, path, timeout=300)
        return struct.unpack_from("<I", data)[0], data[4:].hex()

    def put(self, source, path):
        source = Path(source)
        content = source.read_bytes()
        digest = hashlib.sha256(content).hexdigest()
        offset = 0
        try:
            size, existing_hash = self.file_hash(path)
            if (size, existing_hash) == (len(content), digest):
                return digest
            if 0 < size < len(content) and hashlib.sha256(content[:size]).hexdigest() == existing_hash:
                offset = size
        except DeviceError:
            pass
        if not content:
            self.request(3, path, struct.pack("<IB", 0, 1))
        while offset < len(content):
            chunk = content[offset:offset + 4096]
            if chunk and chunk.count(chunk[0]) == len(chunk):
                stop = offset + len(chunk)
                while stop < len(content) and stop - offset < 1024 * 1024:
                    block = content[stop:stop + 4096]
                    if block.count(chunk[0]) != len(block):
                        break
                    stop += len(block)
                self.request(10, path, struct.pack("<IIBB", offset, stop - offset, chunk[0], offset == 0), timeout=120)
                offset = stop
            else:
                for attempt in range(3):
                    try:
                        self.request(3, path, struct.pack("<IB", offset, offset == 0) + chunk)
                        break
                    except (DeviceError, TimeoutError):
                        existing, size = self.read(path, offset, len(chunk))
                        if existing == chunk:
                            break
                        if (offset and size != offset) or attempt == 2:
                            raise
                offset += len(chunk)
            if offset % (256 * 1024) == 0 or offset == len(content):
                print(f"{path}: {offset}/{len(content)} bytes", flush=True)
        if self.file_hash(path) != (len(content), digest):
            raise DeviceError("Uploaded file hash mismatch")
        return digest

    def screenshot(self, target):
        from PIL import Image
        pixels = bytearray()
        for offset in range(0, 480 * 480 * 2, 4096):
            count = min(4096, 480 * 480 * 2 - offset)
            for attempt in range(3):
                try:
                    data = self.request(13, body=struct.pack("<IH", offset, count))
                    break
                except TimeoutError:
                    if attempt == 2:
                        raise
            if len(data) != count:
                raise DeviceError("Short screenshot response")
            pixels.extend(data)
        rgb = bytearray()
        for (value,) in struct.iter_unpack("<H", pixels):
            rgb.extend((((value >> 11) & 31) * 255 // 31, ((value >> 5) & 63) * 255 // 63,
                        (value & 31) * 255 // 31))
        Image.frombytes("RGB", (480, 480), bytes(rgb)).save(target)

    def tap(self, x, y):
        self.request(14, body=struct.pack("<HHB", x, y, 1))
        time.sleep(0.06)
        self.request(14, body=struct.pack("<HHB", x, y, 0))
        time.sleep(0.15)

    def text(self, text):
        self.request(15, body=text.encode("utf-8"))

    def diagnostics(self):
        values = struct.unpack("<6I", self.request(16))
        return dict(zip(("uptime", "free_heap", "minimum_free_heap", "largest_heap_block", "free_psram", "baud"), values))

    def configure_wifi(self, ssid, password):
        a, b = ssid.encode(), password.encode()
        self.request(11, body=bytes([len(a), len(b)]) + a + b)


def main():
    parser = argparse.ArgumentParser()
    parser.add_argument("--port", default="COM28")
    parser.add_argument("--baud", type=int, default=115200)
    commands = parser.add_subparsers(dest="command", required=True)
    for name in ("info", "sd-info", "diagnostics"):
        commands.add_parser(name)
    for name in ("ls", "hash"):
        commands.add_parser(name).add_argument("path")
    for name in ("get", "put"):
        command = commands.add_parser(name)
        command.add_argument("source")
        command.add_argument("target")
    commands.add_parser("screenshot").add_argument("target")
    tap = commands.add_parser("tap")
    tap.add_argument("x", type=int)
    tap.add_argument("y", type=int)
    commands.add_parser("text").add_argument("value")
    args = parser.parse_args()
    with CairnClient(args.port, args.baud) as device:
        if args.command == "info": result = device.info()
        elif args.command == "sd-info": result = device.sd_info()
        elif args.command == "diagnostics": result = device.diagnostics()
        elif args.command == "ls": result = device.listdir(args.path)
        elif args.command == "hash": result = device.file_hash(args.path)
        elif args.command == "get": result = device.get(args.source, args.target)
        elif args.command == "put": result = device.put(args.source, args.target)
        elif args.command == "screenshot": result = device.screenshot(args.target)
        elif args.command == "tap": result = device.tap(args.x, args.y)
        elif args.command == "text": result = device.text(args.value)
        print(json.dumps(result, indent=2))


if __name__ == "__main__":
    main()
