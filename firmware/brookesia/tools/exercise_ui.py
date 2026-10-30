#!/usr/bin/env python3
"""Exercise touch paths on the real board and retain screenshots for inspection."""
import argparse
import json
from pathlib import Path
import time

from device_ui import Device


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--port", required=True)
    parser.add_argument("--output", required=True)
    args = parser.parse_args()
    output = Path(args.output)
    output.mkdir(parents=True, exist_ok=True)
    device = Device(args.port, output / "serial.log")
    actions = []

    def command(text, delay=0.15):
        reply = device.command(text)
        actions.append({"command": text, "reply": reply})
        print(text, reply, flush=True)
        time.sleep(delay)
        return reply

    def capture(name):
        actions.append({"capture": device.capture(output / (name + ".png"))})
        (output / "actions.json").write_text(json.dumps(actions, indent=2))
        print("Captured", name, flush=True)

    try:
        command("apps", 0.5)
        command("run Calculator", 0.6)
        for touch in ["tap 67 182", "tap 67 374", "tap 412 310", "tap 182 374", "tap 412 438"]:
            command(touch)
        capture("calculator_1_plus_2")
        command("close Calculator", 0.6)

        command("run DrawPanel", 0.6)
        command("drag 100 100 360 300")
        command("tap 152 420")
        command("drag 100 160 330 210", 0.4)
        capture("draw_black_and_red")
        command("tap 372 420", 0.4)
        capture("draw_cleared")
        command("close DrawPanel", 0.6)

        command("run Pomodoro", 0.6)
        capture("pomodoro_before")
        command("tap 308 372", 0.4)
        capture("pomodoro_plus_minute")
        command("tap 174 372")
        command("tap 240 372", 2.4)
        capture("pomodoro_running")
        command("tap 240 372")
        command("tap 118 372", 0.3)
        command("close Pomodoro", 0.6)

        command("run Motion", 0.6)
        for page in range(4):
            capture(f"motion_page_{page}")
            if page < 3:
                command("drag 400 240 80 240", 0.8)
        command("close Motion", 0.6)

        command("run SpecAnalyzer", 0.8)
        for page in range(4):
            capture(f"spectrum_page_{page}")
            if page < 3:
                command("drag 400 240 80 240", 0.8)
        command("close SpecAnalyzer", 0.6)

        command("run Settings", 0.6)
        command("tap 240 175", 0.6)
        capture("settings_wlan")
        command("back", 0.4)
        command("close Settings", 0.6)
        command("stats")
        command("home", 0.4)
        capture("desktop_after_exercise")
    finally:
        device.close()


if __name__ == "__main__":
    main()
