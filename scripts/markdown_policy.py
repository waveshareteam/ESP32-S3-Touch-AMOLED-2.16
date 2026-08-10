#!/usr/bin/env python3
"""Small repository-local policy check used by the lightweight CI gate.

It checks the declared first-party companion pages and obvious public-text
hygiene. Full ownership, fragment, and diff-scope auditing is performed by the
maintainer-facing Waveshare audit documented in docs/ci.md.
"""

from __future__ import annotations

import re
import sys
from pathlib import Path


PAIRS = (
    ("README.md", "README_ZH.md"),
    ("CONTRIBUTING.md", "CONTRIBUTING_ZH.md"),
    ("SUPPORT.md", "SUPPORT_ZH.md"),
    ("docs/ci.md", "docs/ci_ZH.md"),
    ("docs/components.md", "docs/components_ZH.md"),
    ("docs/firmware.md", "docs/firmware_ZH.md"),
    ("docs/repository-structure.md", "docs/repository-structure_ZH.md"),
    ("docs/brookesia.md", "docs/brookesia_ZH.md"),
    ("firmware/README.md", "firmware/README_ZH.md"),
    ("releases/README.md", "releases/README_ZH.md"),
    ("examples/esp-idf/04_Immersive_block/README.md", "examples/esp-idf/04_Immersive_block/README_ZH.md"),
)
SENSITIVE = re.compile(r"(?:\b[A-Z]:[\\/]|/Users/[A-Za-z0-9._-]+/|\\\\[A-Za-z0-9._-]+\\|\bCOM[1-9][0-9]*\b)")


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    failures: list[str] = []
    for english, chinese in PAIRS:
        for relative, peer in ((english, chinese), (chinese, english)):
            page = root / relative
            if not page.is_file():
                failures.append(f"missing first-party page: {relative}")
                continue
            text = page.read_text(encoding="utf-8")
            if Path(peer).name not in text:
                failures.append(f"missing reciprocal language link: {relative} -> {peer}")
            if SENSITIVE.search(text):
                failures.append(f"machine-specific public text: {relative}")
    if failures:
        print("Markdown policy failed:", *failures, sep="\n- ", file=sys.stderr)
        return 1
    print(f"Markdown policy passed for {len(PAIRS)} first-party pairs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
