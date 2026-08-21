#!/usr/bin/env python3
"""Create flashable firmware archives from CI build outputs."""

from __future__ import annotations

import argparse
import json
import re
import shutil
import stat
import sys
import tempfile
import unicodedata
import zipfile
from datetime import datetime, timezone
from pathlib import Path
from typing import Iterable


DEFAULT_BAUD = "460800"


def slugify(value: str) -> str:
    value = unicodedata.normalize("NFKC", value).strip().replace("\\", "/")
    value = re.sub(r"[^A-Za-z0-9._-]+", "-", value)
    value = re.sub(r"-+", "-", value).strip("-")
    return value or "firmware"


def parse_offset(value: str) -> int:
    return int(value, 0)


def safe_project_path(project: Path, repo: Path) -> str:
    try:
        return project.resolve().relative_to(repo.resolve()).as_posix()
    except ValueError:
        return project.name


def quote_shell(value: str) -> str:
    return "'" + value.replace("'", "'\"'\"'") + "'"


def quote_batch(value: str) -> str:
    return '"' + value.replace('"', '\\"') + '"'


def write_text(path: Path, content: str, executable: bool = False) -> None:
    path.write_text(content, encoding="utf-8", newline="\n")
    if executable:
        path.chmod(path.stat().st_mode | stat.S_IXUSR | stat.S_IXGRP | stat.S_IXOTH)


def copy_file(src: Path, firmware_dir: Path, offset: str | None = None) -> str:
    if not src.exists():
        raise FileNotFoundError(f"missing firmware file: {src}")
    if not src.is_file():
        raise ValueError(f"firmware source is not a regular file: {src}")
    prefix = f"{offset.lower()}_" if offset else ""
    dst_name = slugify(prefix + src.name)
    dst = firmware_dir / dst_name
    destination_key = unicodedata.normalize("NFKC", dst_name).casefold()
    if any(unicodedata.normalize("NFKC", path.name).casefold() == destination_key for path in firmware_dir.iterdir()):
        raise ValueError(f"duplicate firmware archive name: {dst_name}")
    shutil.copy2(src, dst)
    return f"bin/{dst_name}"


def resolve_build_file(build_dir: Path, source: str | Path, description: str) -> Path:
    """Resolve a regular firmware file while keeping it inside the build root."""
    build_root = build_dir.resolve()
    candidate = Path(source)
    resolved = candidate.resolve() if candidate.is_absolute() else (build_root / candidate).resolve()
    try:
        resolved.relative_to(build_root)
    except ValueError as exc:
        raise ValueError(f"{description} escapes build directory: {source}") from exc
    if not resolved.exists():
        raise FileNotFoundError(f"missing firmware file: {resolved}")
    if not resolved.is_file():
        raise ValueError(f"firmware source is not a regular file: {resolved}")
    return resolved


def resolve_flash_file(build_dir: Path, source: str) -> Path:
    """Resolve an ESP-IDF flash source and keep it inside the declared build."""
    if not isinstance(source, str):
        raise ValueError(f"invalid flash_files source: {source!r}")
    return resolve_build_file(build_dir, source, "flash_files source")


def esp_idf_flash_entries(build_dir: Path, firmware_dir: Path) -> tuple[list[str], list[dict[str, str]], dict]:
    flasher_args_path = build_dir / "flasher_args.json"
    if not flasher_args_path.exists():
        raise FileNotFoundError(f"missing ESP-IDF flasher args: {flasher_args_path}")

    data = json.loads(flasher_args_path.read_text(encoding="utf-8"))
    flash_files = data.get("flash_files")
    if not isinstance(flash_files, dict) or not flash_files:
        raise ValueError(f"no flash_files found in {flasher_args_path}")

    seen_offsets: set[int] = set()
    ordered_files: list[tuple[int, str, object]] = []
    for offset, rel_path in flash_files.items():
        numeric_offset = parse_offset(offset)
        if numeric_offset in seen_offsets:
            raise ValueError(f"duplicate flash offset: {offset}")
        seen_offsets.add(numeric_offset)
        ordered_files.append((numeric_offset, offset, rel_path))

    entries: list[dict[str, str]] = []
    command_pairs: list[str] = []
    for _, offset, rel_path in sorted(ordered_files):
        src = resolve_flash_file(build_dir, rel_path)
        copied = copy_file(src, firmware_dir, offset)
        rel_source = Path(rel_path)
        source_name = rel_source.name if rel_source.is_absolute() else rel_source.as_posix()
        entries.append({"offset": offset, "file": copied, "source": source_name})
        command_pairs.extend([offset, copied])

    return command_pairs, entries, data


def arduino_flash_entries(build_dir: Path, firmware_dir: Path) -> tuple[list[str], list[dict[str, str]]]:
    build_root = build_dir.resolve()
    bins = sorted(build_root.rglob("*.bin"), key=lambda path: path.as_posix().lower())
    if not bins:
        raise FileNotFoundError(f"no Arduino .bin files found in {build_dir}")

    merged = next((path for path in bins if path.name.endswith(".merged.bin")), None)
    if merged:
        merged = resolve_build_file(build_dir, merged, "Arduino firmware source")
        copied = copy_file(merged, firmware_dir)
        return ["0x0", copied], [{"offset": "0x0", "file": copied, "source": merged.name}]

    selected: list[tuple[str, Path]] = []
    for path in bins:
        name = path.name
        if name.endswith(".bootloader.bin"):
            selected.append(("0x0", path))
        elif name.endswith(".partitions.bin"):
            selected.append(("0x8000", path))
        elif name == "boot_app0.bin" or name.endswith(".boot_app0.bin"):
            selected.append(("0xe000", path))
        elif not any(token in name for token in (".bootloader.", ".partitions.", ".merged.")):
            selected.append(("0x10000", path))

    if not selected:
        raise ValueError(f"could not infer Arduino flash layout from {build_dir}")

    entries: list[dict[str, str]] = []
    command_pairs: list[str] = []
    for offset, src in sorted(selected, key=lambda item: parse_offset(item[0])):
        src = resolve_build_file(build_dir, src, "Arduino firmware source")
        copied = copy_file(src, firmware_dir, offset)
        entries.append({"offset": offset, "file": copied, "source": src.name})
        command_pairs.extend([offset, copied])
    return command_pairs, entries


def build_esptool_prefix(chip: str, before: str, after: str) -> list[str]:
    return [
        "python",
        "-m",
        "esptool",
        "--chip",
        chip,
        "--port",
        "$PORT",
        "--baud",
        DEFAULT_BAUD,
        "--before",
        before,
        "--after",
        after,
        "write_flash",
    ]


def shell_command(parts: Iterable[str]) -> str:
    return " ".join("$PORT" if part == "$PORT" else quote_shell(part) for part in parts)


def batch_command(parts: Iterable[str]) -> str:
    return " ".join("%PORT%" if part == "$PORT" else quote_batch(part) for part in parts)


def write_flash_helpers(package_dir: Path, command: list[str], artifact_name: str) -> None:
    shell = f"""#!/usr/bin/env sh
set -eu
PORT="${{1:-}}"
if [ -z "$PORT" ]; then
    echo "Usage: $0 /dev/ttyUSB0"
    exit 2
fi
cd "$(dirname "$0")"
{shell_command(command)}
"""
    batch = f"""@echo off
set PORT=%1
if "%PORT%"=="" (
  echo Usage: flash.bat COMx
  exit /b 2
)
cd /d %~dp0
{batch_command(command)}
"""
    args_txt = " ".join("<PORT>" if part == "$PORT" else part for part in command) + "\n"
    write_text(package_dir / "flash.sh", shell, executable=True)
    write_text(package_dir / "flash.bat", batch)
    write_text(package_dir / "flash_args.txt", args_txt)
    write_text(
        package_dir / "README.md",
        f"""# {artifact_name}

Install esptool if needed:

```bash
python -m pip install esptool
```

Flash from this directory:

```bash
./flash.sh /dev/ttyUSB0
```

On Windows:

```bat
flash.bat COMx
```
""",
    )


def create_zip(package_dir: Path, zip_path: Path) -> None:
    if zip_path.exists():
        zip_path.unlink()
    with zipfile.ZipFile(zip_path, "w", compression=zipfile.ZIP_DEFLATED) as archive:
        for path in sorted(package_dir.rglob("*"), key=lambda item: item.as_posix()):
            if path.is_file():
                archive.write(path, path.relative_to(package_dir.parent).as_posix())


def remove_output_target(path: Path) -> None:
    """Remove one exact output target without traversing a symlink."""
    if path.is_symlink() or path.is_file():
        path.unlink(missing_ok=True)
    elif path.exists():
        shutil.rmtree(path)


def artifact_paths(output_dir: Path, artifact_name: str) -> tuple[Path, Path]:
    """Return only output targets that are strict direct children of output_dir."""
    normalized_name = unicodedata.normalize("NFKC", artifact_name)
    components = normalized_name.replace("\\", "/").split("/")
    if (
        not normalized_name
        or "/" in normalized_name
        or "\\" in normalized_name
        or any(component in ("", ".", "..") for component in components)
    ):
        raise ValueError(f"invalid artifact name: {artifact_name!r}")

    output_root = output_dir.resolve()
    package_dir = output_dir / normalized_name
    zip_path = output_dir / f"{normalized_name}.zip"
    for path in (package_dir, zip_path):
        resolved = path.resolve()
        if resolved.parent != output_root:
            raise ValueError(f"artifact target escapes output directory: {path}")
    return package_dir, zip_path


def package(args: argparse.Namespace) -> Path:
    repo = Path.cwd()
    project = Path(args.project)
    build_dir = Path(args.build_dir)
    output_dir = Path(args.output_dir)
    requested_name = args.name or f"{project.name}-{args.framework_version or args.framework}"
    if "/" in unicodedata.normalize("NFKC", requested_name) or "\\" in unicodedata.normalize("NFKC", requested_name):
        raise ValueError(f"invalid artifact name: {requested_name!r}")
    artifact_name = slugify(requested_name)
    output_dir.mkdir(parents=True, exist_ok=True)
    package_dir, zip_path = artifact_paths(output_dir, artifact_name)
    staging_root: Path | None = None

    try:
        # The failure contract removes stale exact targets, including failures
        # while creating the staging area. Validation above makes this safe.
        remove_output_target(package_dir)
        remove_output_target(zip_path)
        staging_root = Path(tempfile.mkdtemp(prefix=f".{artifact_name}.", dir=output_dir))
        staged_package_dir = staging_root / artifact_name
        firmware_dir = staged_package_dir / "bin"
        staged_zip_path = staging_root / zip_path.name
        firmware_dir.mkdir(parents=True, exist_ok=True)
        if args.framework == "esp-idf":
            command_pairs, files, flasher_args = esp_idf_flash_entries(build_dir, firmware_dir)
            extra_args = flasher_args.get("extra_esptool_args", {})
            chip = args.target or extra_args.get("chip") or "esp32s3"
            before = extra_args.get("before", "default_reset")
            after = extra_args.get("after", "hard_reset")
            write_flash_args = [str(item) for item in flasher_args.get("write_flash_args", [])]
        else:
            command_pairs, files = arduino_flash_entries(build_dir, firmware_dir)
            chip = args.target or "esp32s3"
            before = "default_reset"
            after = "hard_reset"
            write_flash_args = []

        command = build_esptool_prefix(chip, before, after) + write_flash_args + command_pairs
        manifest = {
            "name": artifact_name,
            "framework": args.framework,
            "framework_version": args.framework_version,
            "target": chip,
            "project": safe_project_path(project, repo),
            "git_sha": args.git_sha,
            "generated_at": datetime.now(timezone.utc).isoformat(),
            "baud": DEFAULT_BAUD,
            "files": files,
            "flash_command": " ".join("<PORT>" if item == "$PORT" else item for item in command),
        }
        write_text(staged_package_dir / "manifest.json", json.dumps(manifest, indent=2) + "\n")
        write_flash_helpers(staged_package_dir, command, artifact_name)
        create_zip(staged_package_dir, staged_zip_path)

        staged_package_dir.replace(package_dir)
        # Path.replace overwrites an existing archive in the same directory
        # atomically, after all package contents have been prepared.
        staged_zip_path.replace(zip_path)
    except BaseException:
        remove_output_target(package_dir)
        remove_output_target(zip_path)
        raise
    finally:
        if staging_root is not None:
            shutil.rmtree(staging_root, ignore_errors=True)

    print(zip_path.as_posix())
    return zip_path


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--framework", choices=("esp-idf", "arduino"), required=True)
    parser.add_argument("--project", required=True, help="Repo-relative project or sketch path.")
    parser.add_argument("--build-dir", required=True, help="Build output directory.")
    parser.add_argument("--output-dir", default="releases/dist")
    parser.add_argument("--name", help="Firmware archive name.")
    parser.add_argument("--framework-version", help="ESP-IDF tag or Arduino core version.")
    parser.add_argument("--target", default="esp32s3")
    parser.add_argument("--git-sha", default="")
    args = parser.parse_args()
    try:
        package(args)
    except Exception as exc:
        print(f"error: {exc}", file=sys.stderr)
        return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
