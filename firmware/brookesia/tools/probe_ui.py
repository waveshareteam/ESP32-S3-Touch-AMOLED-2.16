#!/usr/bin/env python3
"""One bounded pass through installed applications on the connected board."""
import argparse
import json
from pathlib import Path
import re
import time

from device_ui import Device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    parser.add_argument("--apps", help="Comma-separated names; defaults to every installed app")
    parser.add_argument("--sample-seconds", type=float, default=3)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    device = Device(args.port, output / "serial.log")
    results = []
    try:
        applications = []
        for line in device.command("apps"):
            match = re.fullmatch(r"UIDIAG APP (\d+) (.*?)(?: ACTIVE)?", line)
            if match:
                applications.append((int(match[1]), match[2]))
        requested = set(args.apps.split(",")) if args.apps else None
        device.capture(output / "desktop.png")
        for app_id, name in applications:
            if requested and name not in requested:
                continue
            record = {"id": app_id, "name": name}
            print(f"Opening {app_id}: {name}", flush=True)
            record["run"] = device.command(f"run {app_id}")
            time.sleep(1)
            device.command("stats")
            time.sleep(args.sample_seconds)
            record["stats"] = device.command("stats")
            filename = re.sub(r"[^A-Za-z0-9_-]", "_", name) + ".png"
            record["capture"] = device.capture(output / filename)
            record["close"] = device.command(f"close {app_id}")
            time.sleep(0.5)
            record["after_close"] = device.command("stats")
            results.append(record)
            (output / "results.json").write_text(json.dumps(results, indent=2))
            print(json.dumps(record), flush=True)
        device.command("home")
    finally:
        device.close()


if __name__ == "__main__":
    main()
