#!/usr/bin/env python3
"""Classify a complete Git diff and select first-party example CI work.

Routing policy lives in ``config/ci-routing.json``. This helper validates and
consumes that file so the workflow and documented audit do not drift apart.
"""

from __future__ import annotations

import argparse
import fnmatch
import json
import re
import subprocess
import sys
from pathlib import Path, PurePosixPath

from discover_examples import discover_arduino, discover_esp_idf


IDF_VERSIONS = ("v5.5.5", "v6.0.2")
ARDUINO_CORE = "3.3.11"
FQBN = "esp32:esp32:esp32s3"
CONFIG_KEYS = {
    "build_override_patterns",
    "documentation_patterns",
    "documentation_asset_patterns",
    "ignore_build_patterns",
    "firmware_patterns",
    "esp_idf_shared_patterns",
    "arduino_shared_patterns",
    "esp_idf_global_patterns",
    "arduino_global_patterns",
    "global_build_patterns",
}


def norm(path: str) -> str:
    return path.replace("\\", "/").strip("/")


def matches(path: str, patterns: list[str]) -> bool:
    return any(fnmatch.fnmatchcase(norm(path), norm(pattern)) for pattern in patterns)


def load_config(repo: Path, config_path: Path | None = None) -> dict[str, list[str]]:
    """Load the repository's single routing-policy source of truth."""
    path = config_path or repo / "config" / "ci-routing.json"
    try:
        raw = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read routing config {path}: {error}") from error
    if not isinstance(raw, dict):
        raise ValueError("routing config root must be a JSON object")
    unknown = sorted(set(raw) - CONFIG_KEYS)
    missing = sorted(CONFIG_KEYS - set(raw))
    if unknown:
        raise ValueError("unknown routing config keys: " + ", ".join(unknown))
    if missing:
        raise ValueError("missing routing config keys: " + ", ".join(missing))
    config: dict[str, list[str]] = {}
    for key in sorted(CONFIG_KEYS):
        patterns = raw[key]
        if not isinstance(patterns, list) or not all(isinstance(item, str) for item in patterns):
            raise ValueError(f"routing config key {key!r} must contain strings")
        normalized: list[str] = []
        for pattern in patterns:
            value = norm(pattern)
            if not value or re.match(r"^[A-Za-z]:", value) or value.startswith("/"):
                raise ValueError(f"routing config pattern must be repository-relative: {pattern!r}")
            if ".." in PurePosixPath(value.replace("*", "placeholder")).parts:
                raise ValueError(f"routing config pattern must not escape the repository: {pattern!r}")
            normalized.append(value)
        config[key] = normalized
    return config


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


def select_for_path(
    path: str,
    idf: list[dict[str, str]],
    arduino: list[dict[str, str]],
    config: dict[str, list[str]],
) -> tuple[set[str], set[str], str]:
    """Return selected names and a visible reason for one changed path."""
    if not matches(path, config["build_override_patterns"]):
        if matches(path, config["documentation_patterns"]):
            return set(), set(), "documentation"
        if matches(path, config["documentation_asset_patterns"]):
            return set(), set(), "documentation-asset"
    if matches(path, config["firmware_patterns"]):
        return set(), set(), "firmware"
    if matches(path, config["ignore_build_patterns"]):
        return set(), set(), "non-build"
    if matches(path, config["esp_idf_shared_patterns"]):
        return {item["name"] for item in idf}, set(), "esp-idf-shared"
    if matches(path, config["arduino_shared_patterns"]):
        return set(), {item["name"] for item in arduino}, "arduino-shared-library"
    if path.startswith("examples/esp-idf/"):
        parts = norm(path).split("/")
        if len(parts) >= 3:
            name = parts[2]
            if any(item["name"] == name for item in idf):
                return {name}, set(), "esp-idf-project"
            return {item["name"] for item in idf}, set(), "esp-idf-removed-or-unknown-project"
    if path.startswith("examples/arduino/"):
        parts = norm(path).split("/")
        if len(parts) >= 3:
            name = parts[2]
            if any(item["name"] == name for item in arduino):
                return set(), {name}, "arduino-sketch"
            return set(), {item["name"] for item in arduino}, "arduino-removed-or-unknown-sketch"
    if matches(path, config["global_build_patterns"]):
        return {item["name"] for item in idf}, {item["name"] for item in arduino}, "global-build-input"
    if matches(path, config["esp_idf_global_patterns"]):
        return {item["name"] for item in idf}, set(), "esp-idf-shared"
    if matches(path, config["arduino_global_patterns"]):
        return set(), {item["name"] for item in arduino}, "arduino-shared"
    return {item["name"] for item in idf}, {item["name"] for item in arduino}, "unknown-conservative-all"


def classify(repo: Path, paths: list[str], config: dict[str, list[str]] | None = None) -> dict[str, object]:
    if not paths:
        raise ValueError("complete changed-file scope is empty")
    idf = discover_esp_idf(repo)
    arduino = discover_arduino(repo)
    policy = config if config is not None else load_config(repo)
    selected_idf: set[str] = set()
    selected_arduino: set[str] = set()
    reasons: dict[str, str] = {}
    unknown: list[str] = []
    firmware: list[str] = []
    for path in paths:
        route_idf, route_arduino, reason = select_for_path(path, idf, arduino, policy)
        selected_idf.update(route_idf)
        selected_arduino.update(route_arduino)
        reasons[path] = reason
        if reason == "unknown-conservative-all":
            unknown.append(path)
        if matches(path, policy["firmware_patterns"]):
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
    parser.add_argument("--routing-config", type=Path, help="override routing policy path")
    args = parser.parse_args()
    repo = Path(args.repo).resolve()
    try:
        result = classify(repo, changed_paths(repo, args.base), load_config(repo, args.routing_config))
    except ValueError as error:
        print(f"CI routing error: {error}", file=sys.stderr)
        return 2
    if args.github_output:
        write_output(result, args.github_output)
    print(json.dumps(result, indent=2, sort_keys=True))
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
