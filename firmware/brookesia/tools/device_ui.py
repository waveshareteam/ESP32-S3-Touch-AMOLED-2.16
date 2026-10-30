#!/usr/bin/env python3
"""Control the on-device UI diagnostics and save actual LVGL screenshots."""
import argparse
import base64
from pathlib import Path
import re
import time

import serial


class Device:
    def __init__(self, port, log=None):
        self.serial = serial.Serial(port=None, baudrate=115200, timeout=0.25, write_timeout=3, exclusive=True)
        self.serial.dtr = False
        self.serial.rts = False
        self.serial.port = port
        self.serial.open()
        self.log = open(log, "ab", buffering=0) if log else None

    def close(self):
        self.serial.close()
        if self.log:
            self.log.close()

    def line(self):
        raw = self.serial.readline()
        if self.log:
            self.log.write(raw)
        return raw.decode("utf-8", errors="replace").strip()

    def command(self, command, timeout=15):
        self.serial.write((command + "\n").encode())
        deadline = time.monotonic() + timeout
        replies = []
        while time.monotonic() < deadline:
            line = self.line()
            if "UIDIAG " not in line:
                continue
            message = line[line.index("UIDIAG "):]
            replies.append(message)
            if command == "apps":
                if message == "UIDIAG APPS END":
                    return replies
            elif command == "audio_probe":
                if message.startswith(("UIDIAG AUDIO END", "UIDIAG AUDIO ERR", "UIDIAG AUDIO BUSY")):
                    return replies
            else:
                return replies
        raise TimeoutError(command)

    def capture(self, destination, command="capture"):
        from PIL import Image
        self.serial.write((command + "\n").encode())
        deadline = time.monotonic() + 120
        header = None
        chunks = []
        while time.monotonic() < deadline:
            line = self.line()
            if "UIDIAG CAPTURE BEGIN" in line:
                header = dict(re.findall(r"(\w+)=([^ ]+)", line))
            elif "UIDIAG DATA " in line:
                chunks.append(line.split("UIDIAG DATA ", 1)[1].strip())
            elif "UIDIAG CAPTURE END" in line:
                break
            elif "UIDIAG ERR" in line:
                raise RuntimeError(line)
        if header is None:
            raise TimeoutError("capture header")
        raw = base64.b64decode("".join(chunks), validate=True)
        if len(raw) != int(header["bytes"]):
            raise ValueError(f"Incomplete screenshot: {len(raw)}/{header['bytes']}")
        width, height, stride = (int(header[key]) for key in ("w", "h", "stride"))
        rgb = bytearray(width * height * 3)
        for y in range(height):
            for x in range(width):
                offset = y * stride + x * 2
                pixel = raw[offset] | raw[offset + 1] << 8
                out = (y * width + x) * 3
                rgb[out] = ((pixel >> 11) & 31) * 255 // 31
                rgb[out + 1] = ((pixel >> 5) & 63) * 255 // 63
                rgb[out + 2] = (pixel & 31) * 255 // 31
        destination = Path(destination)
        destination.parent.mkdir(parents=True, exist_ok=True)
        Image.frombytes("RGB", (width, height), bytes(rgb)).save(destination)
        return {"path": str(destination), "width": width, "height": height}


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--log")
    parser.add_argument("--capture")
    parser.add_argument("command", nargs="*", default=["stats"])
    args = parser.parse_args()
    device = Device(args.port, args.log)
    try:
        if args.capture:
            print(device.capture(args.capture))
        else:
            print("\n".join(device.command(" ".join(args.command))))
    finally:
        device.close()


if __name__ == "__main__":
    main()
