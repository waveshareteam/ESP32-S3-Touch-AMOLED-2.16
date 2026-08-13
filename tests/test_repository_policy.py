from __future__ import annotations

import importlib.util
import json
import re
import subprocess
import sys
import tempfile
import unittest
import zipfile
from pathlib import Path
from types import SimpleNamespace
from unittest import mock


ROOT = Path(__file__).resolve().parents[1]
SDKCONFIG_ASSIGNMENT = "CONFIG_"


def active_sdkconfig_errors(path: Path) -> list[str]:
    errors: list[str] = []
    for line_number, line in enumerate(path.read_text(encoding="utf-8").splitlines(), start=1):
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if not stripped.startswith(SDKCONFIG_ASSIGNMENT) or "=" not in stripped:
            errors.append(f"{path}:{line_number}: malformed active sdkconfig assignment")
            continue
        key, _ = stripped.split("=", 1)
        if not key.replace("_", "").isalnum() or not key.startswith(SDKCONFIG_ASSIGNMENT):
            errors.append(f"{path}:{line_number}: malformed active sdkconfig assignment")
    return errors


def tracked_sdkconfig_defaults() -> list[Path]:
    output = subprocess.check_output(
        ["git", "ls-files", "-z", "--", ":(glob)examples/esp-idf/**/sdkconfig.defaults"],
        cwd=ROOT,
    )
    return [ROOT / item for item in output.decode().split("\0") if item and "/components/" not in item]


def load_packager():
    spec = importlib.util.spec_from_file_location("package_firmware", ROOT / "releases" / "package_firmware.py")
    assert spec and spec.loader
    module = importlib.util.module_from_spec(spec)
    sys.modules[spec.name] = module
    spec.loader.exec_module(module)
    return module


package_firmware = load_packager()


class SdkconfigPolicyTests(unittest.TestCase):
    def test_tracked_first_party_sdkconfig_defaults_are_valid(self) -> None:
        paths = tracked_sdkconfig_defaults()
        self.assertTrue(paths)
        errors = [error for path in paths for error in active_sdkconfig_errors(path)]
        self.assertEqual(errors, [])

    def test_malformed_active_assignment_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            path = Path(temporary) / "sdkconfig.defaults"
            path.write_text("CONFIG_PMU_I2C_SCL 14\n", encoding="utf-8")
            self.assertEqual(len(active_sdkconfig_errors(path)), 1)

    def test_axp2101_board_constants_match_the_schematic(self) -> None:
        path = ROOT / "examples/esp-idf/01_AXP2101/sdkconfig.defaults"
        values = dict(
            line.split("=", 1)
            for line in path.read_text(encoding="utf-8").splitlines()
            if line and not line.startswith("#")
        )
        self.assertEqual(
            {key: values[key] for key in ("CONFIG_PMU_I2C_SCL", "CONFIG_PMU_I2C_SDA", "CONFIG_PMU_INTERRUPT_PIN")},
            {"CONFIG_PMU_I2C_SCL": "14", "CONFIG_PMU_I2C_SDA": "15", "CONFIG_PMU_INTERRUPT_PIN": "-1"},
        )

    def test_lvgl_qmi8658_selection_matches_the_schematic_address(self) -> None:
        header = (ROOT / "examples/arduino/libraries/SensorLib/src/REG/QMI8658Constants.h").read_text(encoding="utf-8")
        sketch = (ROOT / "examples/arduino/05_LVGL_Widgets/05_LVGL_Widgets.ino").read_text(encoding="utf-8")
        match = re.search(r"^#define\s+QMI8658_L_SLAVE_ADDRESS\s+\(?(0x[0-9A-Fa-f]+)\)?", header, re.MULTILINE)
        self.assertIsNotNone(match)
        self.assertEqual(match.group(1), "0x6B")
        self.assertIn("qmi.begin(Wire, QMI8658_L_SLAVE_ADDRESS, IIC_SDA, IIC_SCL)", sketch)


class FirmwarePackagingTests(unittest.TestCase):
    def write_flasher_args(self, build_dir: Path, flash_files: dict[str, object]) -> None:
        (build_dir / "flasher_args.json").write_text(json.dumps({"flash_files": flash_files}), encoding="utf-8")

    def package_esp_idf(self, build_dir: Path, output_dir: Path, name: str = "test-package") -> Path:
        return package_firmware.package(
            SimpleNamespace(
                framework="esp-idf",
                project=ROOT / "examples/esp-idf/01_AXP2101",
                build_dir=build_dir,
                output_dir=output_dir,
                name=name,
                framework_version="",
                target="esp32s3",
                git_sha="",
            )
        )

    def package_arduino(self, build_dir: Path, output_dir: Path) -> Path:
        return package_firmware.package(
            SimpleNamespace(
                framework="arduino",
                project=ROOT / "examples/arduino/01_HelloWorld",
                build_dir=build_dir,
                output_dir=output_dir,
                name="test-package",
                framework_version="",
                target="esp32s3",
                git_sha="",
            )
        )

    def new_build(self, root: Path) -> Path:
        build_dir = root / "build"
        build_dir.mkdir()
        return build_dir

    def assert_package_failure(self, build_dir: Path, output_dir: Path, message: str) -> None:
        with self.assertRaisesRegex((ValueError, FileNotFoundError), message):
            self.package_esp_idf(build_dir, output_dir)
        self.assertFalse((output_dir / "test-package.zip").exists())
        self.assertFalse((output_dir / "test-package").exists())

    def test_valid_nested_flash_file_is_packaged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            (build_dir / "nested").mkdir()
            (build_dir / "nested/app.bin").write_bytes(b"firmware")
            self.write_flasher_args(build_dir, {"0x10000": "nested/app.bin"})
            archive = self.package_esp_idf(build_dir, root / "output")
            self.assertTrue(archive.is_file())
            with zipfile.ZipFile(archive) as package:
                self.assertIn("test-package/bin/0x10000_app.bin", package.namelist())
                self.assertEqual(package.read("test-package/bin/0x10000_app.bin"), b"firmware")
                manifest = json.loads(package.read("test-package/manifest.json"))
                self.assertEqual(manifest["files"], [{"offset": "0x10000", "file": "bin/0x10000_app.bin", "source": "nested/app.bin"}])
                self.assertIn("bin/0x10000_app.bin", package.read("test-package/flash.sh").decode())
                self.assertIn("bin/0x10000_app.bin", package.read("test-package/flash.bat").decode())
                self.assertIn("<PORT>", package.read("test-package/flash_args.txt").decode())

    def test_existing_archive_is_replaced_after_a_successful_package(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            output_dir = root / "output"
            output_dir.mkdir()
            (output_dir / "test-package.zip").write_bytes(b"stale zip")
            (build_dir / "app.bin").write_bytes(b"firmware")
            self.write_flasher_args(build_dir, {"0x10000": "app.bin"})
            archive = self.package_esp_idf(build_dir, output_dir)
            self.assertTrue(zipfile.is_zipfile(archive))
            with zipfile.ZipFile(archive) as package:
                self.assertEqual(package.read("test-package/bin/0x10000_app.bin"), b"firmware")

    def test_absolute_inside_build_file_is_packaged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            source = build_dir / "nested" / "app.bin"
            source.parent.mkdir()
            source.write_bytes(b"firmware")
            self.write_flasher_args(build_dir, {"0x10000": str(source.resolve())})
            archive = self.package_esp_idf(build_dir, root / "output")
            with zipfile.ZipFile(archive) as package:
                self.assertIn("test-package/bin/0x10000_app.bin", package.namelist())

    def test_symlinked_build_dir_is_packaged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            real_build = self.new_build(root)
            link_build = root / "build-link"
            try:
                link_build.symlink_to(real_build, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"symlinks unavailable: {exc}")
            (real_build / "app.bin").write_bytes(b"firmware")
            self.write_flasher_args(real_build, {"0x10000": "app.bin"})
            archive = self.package_esp_idf(link_build, root / "output")
            self.assertTrue(archive.is_file())

    def test_relative_traversal_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            (root / "outside.bin").write_bytes(b"outside")
            self.write_flasher_args(build_dir, {"0x0": "../outside.bin"})
            self.assert_package_failure(build_dir, root / "output", "escapes build directory")

    def test_absolute_outside_path_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            outside = root / "outside.bin"
            outside.write_bytes(b"outside")
            self.write_flasher_args(build_dir, {"0x0": str(outside)})
            self.assert_package_failure(build_dir, root / "output", "escapes build directory")

    def test_symlink_escape_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            outside = root / "outside.bin"
            outside.write_bytes(b"outside")
            link = build_dir / "linked.bin"
            try:
                link.symlink_to(outside)
            except OSError as exc:
                self.skipTest(f"symlinks unavailable: {exc}")
            self.write_flasher_args(build_dir, {"0x0": "linked.bin"})
            self.assert_package_failure(build_dir, root / "output", "escapes build directory")

    def test_missing_file_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            self.write_flasher_args(build_dir, {"0x10000": "missing.bin"})
            self.assert_package_failure(build_dir, root / "output", "missing firmware file")

    def test_non_regular_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            (build_dir / "directory.bin").mkdir()
            self.write_flasher_args(build_dir, {"0x10000": "directory.bin"})
            self.assert_package_failure(build_dir, root / "output", "not a regular file")

    def test_non_string_source_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            self.write_flasher_args(build_dir, {"0x10000": 123})
            self.assert_package_failure(build_dir, root / "output", "invalid flash_files source")

    def test_casefolded_archive_name_collision_is_rejected(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            first = build_dir / "first" / "app.bin"
            second = build_dir / "second" / "APP.bin"
            first.parent.mkdir()
            second.parent.mkdir()
            first.write_bytes(b"first")
            second.write_bytes(b"second")
            with self.assertRaisesRegex(ValueError, "duplicate firmware archive name"):
                self.package_arduino(build_dir, root / "output")
            self.assertFalse((root / "output" / "test-package.zip").exists())
            self.assertFalse((root / "output" / "test-package").exists())

    def test_duplicate_numeric_flash_offset_is_rejected_before_copying(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            first = build_dir / "first.bin"
            second = build_dir / "second.bin"
            first.write_bytes(b"first")
            second.write_bytes(b"second")
            self.write_flasher_args(build_dir, {"0x10000": "first.bin", "0X10000": "second.bin"})
            self.assert_package_failure(build_dir, root / "output", "duplicate flash offset")

    def test_arduino_symlink_escapes_are_not_packaged(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            output_dir = root / "output"
            outside = root / "outside.bin"
            outside.write_bytes(b"outside firmware")
            try:
                (build_dir / "leak.bin").symlink_to(outside)
            except OSError as exc:
                self.skipTest(f"symlinks unavailable: {exc}")
            with self.assertRaisesRegex(ValueError, "Arduino firmware source escapes build directory"):
                self.package_arduino(build_dir, output_dir)
            self.assertFalse((output_dir / "test-package.zip").exists())
            self.assertFalse((output_dir / "test-package").exists())

            directory_escape = root / "directory-escape"
            directory_escape.mkdir()
            (directory_escape / "escaped.bin").write_bytes(b"directory escape")
            directory_link = build_dir / "leak-directory"
            directory_link.symlink_to(directory_escape, target_is_directory=True)
            discovered = directory_link / "escaped.bin"
            if discovered in build_dir.rglob("*.bin"):
                (build_dir / "leak.bin").unlink()
                with self.assertRaisesRegex(ValueError, "Arduino firmware source escapes build directory"):
                    self.package_arduino(build_dir, output_dir)

    def test_invalid_artifact_names_leave_neighboring_sentinels_untouched(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            (build_dir / "app.bin").write_bytes(b"firmware")
            self.write_flasher_args(build_dir, {"0x10000": "app.bin"})
            output_dir = root / "output"
            sentinel = root / "sentinel.bin"
            sentinel.write_bytes(b"do not touch")
            for name in (".", "..", "nested/name", "nested\\name", "．．"):
                with self.subTest(name=name):
                    with self.assertRaisesRegex(ValueError, "invalid artifact name"):
                        self.package_esp_idf(build_dir, output_dir, name)
                    self.assertEqual(sentinel.read_bytes(), b"do not touch")
                    self.assertFalse((output_dir / "test-package.zip").exists())
                    self.assertFalse((output_dir / "test-package").exists())

    def test_output_target_symlink_is_rejected_without_touching_its_target(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            (build_dir / "app.bin").write_bytes(b"firmware")
            self.write_flasher_args(build_dir, {"0x10000": "app.bin"})
            output_dir = root / "output"
            output_dir.mkdir()
            sentinel_dir = root / "sentinel-package"
            sentinel_dir.mkdir()
            sentinel = sentinel_dir / "sentinel.bin"
            sentinel.write_bytes(b"do not touch")
            try:
                (output_dir / "test-package").symlink_to(sentinel_dir, target_is_directory=True)
            except OSError as exc:
                self.skipTest(f"symlinks unavailable: {exc}")
            with self.assertRaisesRegex(ValueError, "artifact target escapes output directory"):
                self.package_esp_idf(build_dir, output_dir)
            self.assertEqual(sentinel.read_bytes(), b"do not touch")

    def test_staging_creation_failure_removes_only_stale_targets(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            output_dir = root / "output"
            output_dir.mkdir()
            (output_dir / "test-package.zip").write_bytes(b"stale zip")
            stale_package = output_dir / "test-package"
            stale_package.mkdir()
            (stale_package / "stale.bin").write_bytes(b"stale package")
            sentinel = output_dir / "unrelated.bin"
            sentinel.write_bytes(b"do not touch")
            with mock.patch.object(package_firmware.tempfile, "mkdtemp", side_effect=OSError("staging failed")):
                with self.assertRaisesRegex(OSError, "staging failed"):
                    self.package_esp_idf(build_dir, output_dir)
            self.assertFalse((output_dir / "test-package.zip").exists())
            self.assertFalse((output_dir / "test-package").exists())
            self.assertEqual(sentinel.read_bytes(), b"do not touch")

    def test_failure_removes_stale_targets_and_staging(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            root = Path(temporary)
            build_dir = self.new_build(root)
            output_dir = root / "output"
            output_dir.mkdir()
            (output_dir / "test-package.zip").write_bytes(b"stale zip")
            (output_dir / "test-package").mkdir()
            (output_dir / "test-package" / "partial.bin").write_bytes(b"partial package")
            (build_dir / "first").mkdir()
            (build_dir / "second").mkdir()
            (build_dir / "first/app.bin").write_bytes(b"first")
            (build_dir / "second/APP.bin").write_bytes(b"second")
            self.write_flasher_args(build_dir, {"0x10000": "first/app.bin", "0X10000": "second/APP.bin"})
            self.assert_package_failure(build_dir, output_dir, "duplicate flash offset")
            self.assertEqual(list(output_dir.glob(".test-package.*")), [])


if __name__ == "__main__":
    unittest.main()
