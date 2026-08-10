#!/usr/bin/env python3
"""Focused repository-local Markdown gate for first-party public pages."""

from __future__ import annotations

import json
import re
import sys
from pathlib import Path, PurePosixPath


STATIC_PAIRS = (
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
HERO_IMAGE = "docs/assets/ESP32-S3-Touch-AMOLED-2.16-details-1.jpg"
PRODUCT_PAGE = "https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm"
EXPECTED_ALT = {
    "README.md": "Two perspective product renders of the Waveshare ESP32-S3-Touch-AMOLED-2.16 development board",
    "README_ZH.md": "Waveshare ESP32-S3-Touch-AMOLED-2.16 开发板的两个透视角度产品渲染图",
}
QUICK_LINKS = (
    ("product", "🌐", re.compile(r"product|产品", re.I)),
    ("firmware", "📦", re.compile(r"firmware|固件", re.I)),
    ("esp_idf", "🧩", re.compile(r"esp[- ]idf", re.I)),
    ("arduino", "🔧", re.compile(r"arduino", re.I)),
    ("documentation", "📚", re.compile(r"documentation|documents?|docs\b|文档|资料", re.I)),
)
BADGE_MATCHERS = {
    "build": re.compile(r"build|actions?|workflow|\bci\b|构建", re.I),
    "release": re.compile(r"release|firmware|version|固件|发布|版本", re.I),
    "license": re.compile(r"licen[cs]e|许可证|许可", re.I),
}
PROFILE_COMPONENTS = {
    "single-product": {
        "centered_header", "html_h1", "subtitle", "badges", "language_switch",
        "quick_links", "hero_image", "separator", "h2",
    },
}
IMG_RE = re.compile(r"<img\b(?P<attrs>[^>]*)>", re.I)
ATTR_RE = re.compile(r'''(?P<name>[\w:-]+)\s*=\s*["'](?P<value>.*?)["']''')
ANCHOR_TEXT_RE = re.compile(r"<a\b[^>]*>([^<]+)</a>", re.I)
HTML_HREF_RE = re.compile(r'''<a\b[^>]*\bhref\s*=\s*["'](?P<href>.*?)["'][^>]*>''', re.I)
MARKDOWN_LINK_RE = re.compile(r"\[[^\]]*\]\((?P<href>[^\s)]+)(?:\s+[^)]*)?\)")
H2_RE = re.compile(r"^##(?!#)\s+(.+?)\s*$", re.MULTILINE)
EMOJI_RE = re.compile(r"[\U0001F000-\U0001FAFF\u2139\u2600-\u27BF]")


def attrs(tag: str) -> dict[str, str]:
    return {match["name"].lower(): match["value"] for match in ATTR_RE.finditer(tag)}


def normalize_link(source: str, href: str) -> str | None:
    target = href.split("#", 1)[0].split("?", 1)[0].replace("\\", "/")
    if not target or "://" in target or target.startswith(("#", "//", "mailto:")):
        return None
    parts: list[str] = []
    for part in (*PurePosixPath(source).parent.parts, *PurePosixPath(target).parts):
        if part in {"", "."}:
            continue
        if part == "..":
            if not parts:
                return None
            parts.pop()
        else:
            parts.append(part)
    return "/".join(parts)


def links_to(source: str, text: str, peer: str) -> bool:
    hrefs = [match["href"] for match in HTML_HREF_RE.finditer(text)]
    hrefs.extend(match["href"] for match in MARKDOWN_LINK_RE.finditer(text))
    return any(normalize_link(source, href) == peer for href in hrefs)


def load_contract(root: Path) -> tuple[dict, tuple[tuple[str, str], ...]]:
    path = root / "config" / "markdown-audit.json"
    try:
        config = json.loads(path.read_text(encoding="utf-8"))
    except (OSError, json.JSONDecodeError) as error:
        raise ValueError(f"cannot read homepage policy config: {error}") from error
    pairs = config.get("bilingual_pairs")
    homepages = config.get("homepage_pairs")
    if not isinstance(pairs, list) or not isinstance(homepages, list):
        raise ValueError("homepage policy config must declare bilingual and homepage pairs")
    bilingual = tuple(
        (entry["english"], entry["chinese"])
        for entry in pairs
        if isinstance(entry, dict) and isinstance(entry.get("english"), str) and isinstance(entry.get("chinese"), str)
    )
    if len(bilingual) != len(pairs):
        raise ValueError("bilingual pairs must contain English and Chinese paths")
    matching = [entry for entry in homepages if entry.get("english") == "README.md" and entry.get("chinese") == "README_ZH.md"]
    if len(matching) != 1:
        raise ValueError("homepage policy config must declare exactly one README pair")
    contract = matching[0]
    if contract.get("profile") != "single-product":
        raise ValueError("homepage policy config must select the single-product profile")
    for key in ("required_components", "required_quick_links", "required_badges", "required_h2_icons"):
        if not isinstance(contract.get(key), list) or not all(isinstance(item, str) for item in contract[key]):
            raise ValueError(f"homepage policy config key {key!r} must contain strings")
    return contract, bilingual


def heading_icon(title: str) -> str:
    value = title.strip()
    return value.split(maxsplit=1)[0] if EMOJI_RE.match(value) else ""


def check_homepage(root: Path, relative: str, peer: str, contract: dict, failures: list[str]) -> None:
    text = (root / relative).read_text(encoding="utf-8")
    header_end = text.lower().find("</div>")
    header = text[:header_end + len("</div>")] if header_end >= 0 else ""
    h1 = re.search(r"<h1\b[^>]*>(.*?)</h1>", header, re.I | re.S)
    subtitle = re.search(r"<strong\b[^>]*>(.*?)</strong>", header, re.I | re.S)
    images = [attrs(match.group(0)) for match in IMG_RE.finditer(header)]
    hero = next((image for image in images if image.get("src") == HERO_IMAGE), None)
    component_state = {
        "centered_header": bool(re.search(r"<div\b[^>]*\balign\s*=\s*[\"']center[\"']", header, re.I)),
        "html_h1": h1 is not None and not EMOJI_RE.search(re.sub(r"<[^>]+>", "", h1.group(1))),
        "subtitle": subtitle is not None and bool(subtitle.group(1).strip()),
        "badges": bool(images),
        "language_switch": links_to(relative, header, peer),
        "quick_links": bool(ANCHOR_TEXT_RE.findall(header)),
        "hero_image": hero is not None and bool(hero.get("alt", "").strip()) and (root / HERO_IMAGE).is_file(),
        "separator": bool(header and re.match(r"(?:[ \t]*\r?\n)*[ \t]*---[ \t]*(?:\r?\n|$)", text[header_end + len("</div>"):])),
        "h2": bool(H2_RE.findall(text)),
    }
    required_components = PROFILE_COMPONENTS[contract["profile"]] | set(contract["required_components"])
    for component in sorted(required_components):
        if not component_state.get(component, False):
            failures.append(f"missing homepage component {component}: {relative}")
    if hero is not None and hero.get("alt") != EXPECTED_ALT[relative]:
        failures.append(f"inaccurate localized hero alt text: {relative}")
    for role in contract["required_badges"]:
        matcher = BADGE_MATCHERS.get(role)
        if matcher is None or not any(matcher.search(image.get("alt", "") + " " + image.get("src", "")) for image in images):
            failures.append(f"missing {role} badge: {relative}")
    actual_quick: list[str] = []
    for label in ANCHOR_TEXT_RE.findall(header):
        normalized = label.strip()
        for key, icon, pattern in QUICK_LINKS:
            if pattern.search(normalized):
                if not normalized.startswith(icon):
                    failures.append(f"wrong quick-link icon for {key}: {relative}")
                actual_quick.append(key)
                break
    if actual_quick != contract["required_quick_links"]:
        failures.append(f"homepage quick-link sequence differs from config: {relative}")
    if PRODUCT_PAGE not in header:
        failures.append(f"missing official product quick link: {relative}")
    actual_h2 = [heading_icon(title) for title in H2_RE.findall(text)]
    if actual_h2 != contract["required_h2_icons"]:
        failures.append(f"homepage H2 emoji sequence differs from config: {relative}")


def validate(root: Path) -> tuple[list[str], int]:
    contract, configured_pairs = load_contract(root)
    pairs = STATIC_PAIRS + configured_pairs
    failures: list[str] = []
    for english, chinese in pairs:
        for relative, peer in ((english, chinese), (chinese, english)):
            page = root / relative
            if not page.is_file():
                failures.append(f"missing first-party page: {relative}")
                continue
            text = page.read_text(encoding="utf-8")
            if not links_to(relative, text, peer):
                failures.append(f"missing reciprocal language link: {relative} -> {peer}")
            if SENSITIVE.search(text):
                failures.append(f"machine-specific public text: {relative}")
    check_homepage(root, "README.md", "README_ZH.md", contract, failures)
    check_homepage(root, "README_ZH.md", "README.md", contract, failures)
    return failures, len(pairs)


def main() -> int:
    root = Path(__file__).resolve().parents[1]
    try:
        failures, pair_count = validate(root)
    except ValueError as error:
        print(f"Markdown policy failed: {error}", file=sys.stderr)
        return 1
    if failures:
        print("Markdown policy failed:", *failures, sep="\n- ", file=sys.stderr)
        return 1
    print(f"Markdown policy passed for {pair_count} first-party pairs.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
