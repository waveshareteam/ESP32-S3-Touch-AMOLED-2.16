#!/usr/bin/env python3
"""Classify a complete Git diff and select first-party example CI work.

This is intentionally a routing helper, not a replacement for a full Markdown
audit.  It fails when the base/head range cannot be inspected.
"""

from __future__ import annotations

import argparse
import json
import subprocess
import sys
from pathlib import Path

from discover_examples import discover_arduino, discover_esp_idf


IDF_VERSIONS = ("v5.5.5", "v6.0.2")
ARDUINO_CORE = "3.3.11"
FQBN = "esp32:esp32:esp32s3"
DOC_SUFFIXES = {".md", ".markdown", ".rst"}


def norm(path: str) -> str:
    return path.replace("\\", "/").strip("/")


def changed_paths(repo: Path, base: str) -> list[str]:
    """Return old and new paths so renames/deletions retain their impact."""
    probe = subprocess.run(
        ["git", "rev-parse", "--verify", f"{base}^{{commit}}"], cwd=repo,
        text=True, capture_output=True,
    )
    if probe.returncode:
        raise ValueError(f"base revision is unavailable: {base}")
    result = subprocess.run(
        ["git", "diff", "--name-status", "-M", base, "HEAD"], cwd=repo,
        text=True, capture_output=True,
    )
    if result.returncode:
        raise ValueError("unable to read the complete base/head diff")
    paths: list[str] = []
    for line in result.stdout.splitlines():
        fields = line.split("\t")
        if not fields or not fields[0]:
            continue
        if fields[0].startswith(("R", "C")) and len(fields) >= 3:
            paths.extend((norm(fields[1]), norm(fields[2])))
        elif len(fields) >= 2:
            paths.append(norm(fields[1]))
    if not paths:
        raise ValueError("complete base/head diff is empty")
    return paths


def select_for_path(path: str, idf: list[dict[str, str]], arduino: list[dict[str, str]]) -> tuple[set[str], set[str], str]:
    """Return selected names and a visible reason for one changed path."""
    suffix = Path(path).suffix.lower()
    if path.startswith("firmware/"):
        return set(), set(), "firmware"
    if suffix in DOC_SUFFIXES:
        return set(), set(), "documentation"
    if path.startswith("examples/arduino/libraries/"):
        return set(), {item["name"] for item in arduino}, "arduino-shared-library"
    if path.startswith("examples/esp-idf/"):
        remainder = path.split("/")
        if len(remainder) >= 3:
            name = remainder[2]
            if any(item["name"] == name for item in idf):
                return {name}, set(), "esp-idf-project"
            return {item["name"] for item in idf}, set(), "esp-idf-removed-or-unknown-project"
    if path.startswith("examples/arduino/"):
        remainder = path.split("/")
        if len(remainder) >= 3:
            name = remainder[2]
            if any(item["name"] == name for item in arduino):
                return set(), {name}, "arduino-sketch"
            return set(), {item["name"] for item in arduino}, "arduino-removed-or-unknown-sketch"
    if path.startswith("config/"):
        return {item["name"] for item in idf}, set(), "esp-idf-shared"
    if (path.startswith(".github/workflows/") or path.startswith("scripts/")
            or path.startswith("tests/") or path.startswith("releases/")):
        return {item["name"] for item in idf}, {item["name"] for item in arduino}, "global-build-input"
    return {item["name"] for item in idf}, {item["name"] for item in arduino}, "unknown-conservative-all"


def classify(repo: Path, paths: list[str]) -> dict[str, object]:
    idf = discover_esp_idf(repo)
    arduino = discover_arduino(repo)
    selected_idf: set[str] = set()
    selected_arduino: set[str] = set()
    reasons: dict[str, str] = {}
    unknown: list[str] = []
    firmware: list[str] = []
    for path in paths:
        route_idf, route_arduino, reason = select_for_path(path, idf, arduino)
        selected_idf.update(route_idf)
        selected_arduino.update(route_arduino)
        reasons[path] = reason
        if reason == "unknown-conservative-all":
            unknown.append(path)
        if reason == "firmware":
            firmware.append(path)
    selected_idf_entries = [item for item in idf if item["name"] in selected_idf]
    selected_arduino_entries = [item for item in arduino if item["name"] in selected_arduino]
    idf_matrix = [item | {"idf": version} for item in selected_idf_entries for version in IDF_VERSIONS]
    arduino_matrix = [item | {"core": ARDUINO_CORE, "fqbn": FQBN} for item in selected_arduino_entries]
    return {
        "paths": paths, "reasons": reasons, "unknown_paths": unknown,
        "firmware_paths": firmware,
        "esp_idf_matrix": {"include": idf_matrix},
        "arduino_matrix": {"include": arduino_matrix},
        "esp_idf_count": len(idf_matrix), "arduino_count": len(arduino_matrix),
    }


def write_output(result: dict[str, object], filename: str) -> None:
    values = {
        "esp_idf_matrix": json.dumps(result["esp_idf_matrix"], separators=(",", ":")),
        "arduino_matrix": json.dumps(result["arduino_matrix"], separators=(",", ":")),
        "esp_idf_count": str(result["esp_idf_count"]),
        "arduino_count": str(result["arduino_count"]),
        "firmware_changed": str(bool(result["firmware_paths"])).lower(),
        "unknown_paths": json.dumps(result["unknown_paths"], separators=(",", ":")),
    }
    with open(filename, "a", encoding="utf-8") as output:
        for key, value in values.items():
            output.write(f"{key}={value}\n")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=".")
    parser.add_argument("--base", required=True, help="verified Git base revision")
    parser.add_argument("--github-output")
    args = parser.parse_args()
    try:
        result = classify(Path(args.repo).resolve(), changed_paths(Path(args.repo).resolve(), args.base))
    except ValueError as error:
        print(f"CI routing error: {error}", file=sys.stderr)
        return 2
    if args.github_output:
        write_output(result, args.github_output)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
