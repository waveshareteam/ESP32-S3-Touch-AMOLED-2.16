#!/usr/bin/env python3
"""Discover Arduino examples that should be built by CI."""

from __future__ import annotations

import argparse
import fnmatch
import json
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path


GLOBAL_EXAMPLE_PATTERNS = (
    ".github/workflows/arduino-examples.yml",
    ".github/scripts/discover_arduino_examples.py",
)
DEFAULT_CORE_VERSION = "3.3.10"
DEFAULT_FQBN = "esp32:esp32:esp32s3"
DEFAULT_BOARD_OPTIONS = ",".join(
    (
        "FlashSize=16M",
        "PartitionScheme=app3M_fat9M_16MB",
        "PSRAM=enabled",
        "USBMode=hwcdc",
        "CDCOnBoot=cdc",
    )
)


def normalize_path(value: str) -> str:
    return value.replace("\\", "/").strip().strip("/")


def run_git(args: list[str]) -> list[str]:
    result = subprocess.run(
        ["git", *args],
        check=True,
        text=True,
        stdout=subprocess.PIPE,
    )
    return [normalize_path(line) for line in result.stdout.splitlines() if line.strip()]


def is_arduino_root(path: Path) -> bool:
    normalized = path.name.lower().replace("_", "-")
    return path.is_dir() and normalized.startswith("arduino")


def discover_roots() -> list[Path]:
    examples = Path("examples")
    if not examples.is_dir():
        return []

    return sorted(
        (path for path in examples.iterdir() if is_arduino_root(path)),
        key=lambda item: item.as_posix().lower(),
    )


def is_sketch_dir(path: Path) -> bool:
    return path.is_dir() and any(child.suffix.lower() == ".ino" for child in path.iterdir() if child.is_file())


def list_examples() -> list[str]:
    examples: list[str] = []
    for root in discover_roots():
        examples_root = root / "examples"
        if not examples_root.is_dir():
            continue

        for path in examples_root.iterdir():
            if is_sketch_dir(path):
                examples.append(path.as_posix())

    return sorted(dict.fromkeys(examples))


def arduino_root_for_example(example: str) -> str:
    path = Path(example)
    return path.parents[1].as_posix()


def group_examples_by_root(known_examples: set[str]) -> dict[str, list[str]]:
    grouped: dict[str, list[str]] = defaultdict(list)
    for example in sorted(known_examples):
        grouped[arduino_root_for_example(example)].append(example)
    return dict(grouped)


def normalize_example(value: str, known_examples: set[str]) -> str:
    value = normalize_path(value)
    if not value or value == "all":
        return value

    if value in known_examples:
        return value

    if value.endswith(".ino"):
        sketch_dir = Path(value).parent.as_posix()
        if sketch_dir in known_examples:
            return sketch_dir

    matches = [
        example
        for example in known_examples
        if Path(example).name == value or f"{Path(example).name}.ino" == value
    ]
    if len(matches) == 1:
        return matches[0]

    return value


def discover_from_paths(paths: list[str], known_examples: set[str]) -> list[str]:
    selected: set[str] = set()
    roots = [root.as_posix() for root in discover_roots()]
    examples_by_root = group_examples_by_root(known_examples)

    for changed_path in paths:
        changed_path = normalize_path(changed_path)
        if any(fnmatch.fnmatch(changed_path, pattern) for pattern in GLOBAL_EXAMPLE_PATTERNS):
            selected.update(known_examples)
            continue

        matched_known_example = False
        for example in known_examples:
            if changed_path == example or changed_path.startswith(example + "/"):
                selected.add(example)
                matched_known_example = True
                break
            if changed_path.endswith(".ino") and Path(changed_path).parent.as_posix() == example:
                selected.add(example)
                matched_known_example = True
                break

        if matched_known_example:
            continue

        for root in roots:
            root_examples = examples_by_root.get(root, [])
            if changed_path == root:
                selected.update(root_examples)
                break
            if changed_path == f"{root}/libraries" or changed_path.startswith(f"{root}/libraries/"):
                selected.update(root_examples)
                break
            if changed_path == f"{root}/examples" or changed_path.startswith(f"{root}/examples/"):
                selected.update(root_examples)
                break
            if changed_path.startswith(root + "/"):
                selected.update(root_examples)
                break

    return sorted(selected)


def discover_changed_examples(base_ref: str | None, head_ref: str, known_examples: set[str]) -> list[str]:
    if base_ref:
        diff_args = ["diff", "--name-only", f"{base_ref}...{head_ref}"]
    else:
        diff_args = ["diff-tree", "--no-commit-id", "--name-only", "-r", head_ref]

    return discover_from_paths(run_git(diff_args), known_examples)


def github_output(name: str, value: str) -> None:
    output_path = os.environ.get("GITHUB_OUTPUT")
    if output_path:
        with open(output_path, "a", encoding="utf-8") as output:
            output.write(f"{name}={value}\n")


def build_matrix(selected: list[str]) -> dict[str, list[dict[str, str]]]:
    return {
        "include": [
            {
                "example": example,
                "arduino_root": arduino_root_for_example(example),
                "library_path": f"{arduino_root_for_example(example)}/libraries",
                "core_version": DEFAULT_CORE_VERSION,
                "fqbn": DEFAULT_FQBN,
                "board_options": DEFAULT_BOARD_OPTIONS,
            }
            for example in selected
        ]
    }


def main() -> int:
    parser = argparse.ArgumentParser()
    parser.add_argument("--base-ref")
    parser.add_argument("--head-ref", default="HEAD")
    parser.add_argument("--example", default="")
    parser.add_argument(
        "--fallback-all",
        action="store_true",
        help="Build all Arduino examples when no changed example is detected.",
    )
    args = parser.parse_args()

    known_examples = set(list_examples())
    requested_example = normalize_example(args.example, known_examples)

    if requested_example == "all":
        selected = sorted(known_examples)
    elif requested_example:
        if requested_example not in known_examples:
            print(f"Unknown Arduino example: {args.example}", file=sys.stderr)
            print("Known examples:", file=sys.stderr)
            for example in sorted(known_examples):
                print(f"  {example}", file=sys.stderr)
            return 1
        selected = [requested_example]
    else:
        selected = discover_changed_examples(args.base_ref, args.head_ref, known_examples)
        if args.fallback_all and not selected:
            selected = sorted(known_examples)

    matrix = build_matrix(selected)
    matrix_json = json.dumps(matrix, separators=(",", ":"))
    has_examples = "true" if selected else "false"

    github_output("matrix", matrix_json)
    github_output("has_examples", has_examples)
    github_output("examples", ",".join(selected))

    print(matrix_json)
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
