from __future__ import annotations

import sys
import subprocess
import tempfile
import unittest
from types import SimpleNamespace
from unittest.mock import patch
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "scripts"))

import ci_routing
import discover_examples


class DiscoveryTests(unittest.TestCase):
    def test_discovery_counts_and_selectors(self) -> None:
        self.assertEqual(len(discover_examples.discover_esp_idf(ROOT)), 5)
        self.assertEqual(len(discover_examples.discover_arduino(ROOT)), 7)
        entry = {"name": "01_HelloWorld", "path": "examples/arduino/01_HelloWorld"}
        for selector in ("01_HelloWorld", entry["path"], "examples/arduino"):
            self.assertTrue(discover_examples.selector_matches(entry, selector))
        self.assertFalse(discover_examples.selector_matches(entry, "libraries"))
        self.assertEqual(discover_examples.IDF_VERSIONS if hasattr(discover_examples, "IDF_VERSIONS") else (), ())


class RoutingTests(unittest.TestCase):
    def setUp(self) -> None:
        self.idf = discover_examples.discover_esp_idf(ROOT)
        self.arduino = discover_examples.discover_arduino(ROOT)

    def route(self, path: str) -> tuple[set[str], set[str], str]:
        return ci_routing.select_for_path(path, self.idf, self.arduino)

    def test_documentation_does_not_build_examples(self) -> None:
        for path in ("README.md", "examples/esp-idf/04_Immersive_block/README.md", "examples/arduino/01_HelloWorld/README.md", "examples/arduino/libraries/GFX_Library_for_Arduino/README.md"):
            idf, arduino, _ = self.route(path)
            self.assertFalse(idf | arduino, path)

    def test_direct_shared_global_and_firmware_contract(self) -> None:
        idf, arduino, reason = self.route("examples/esp-idf/04_Immersive_block/main/main.c")
        self.assertEqual((idf, arduino, reason), ({"04_Immersive_block"}, set(), "esp-idf-project"))
        idf, arduino, reason = self.route("examples/arduino/01_HelloWorld/01_HelloWorld.ino")
        self.assertEqual((idf, arduino, reason), (set(), {"01_HelloWorld"}, "arduino-sketch"))
        self.assertEqual(len(self.route("examples/arduino/libraries/GFX_Library_for_Arduino/src/Arduino_GFX.cpp")[1]), 7)
        self.assertEqual(len(self.route(".github/workflows/examples.yml")[0]), 5)
        self.assertEqual(self.route("firmware/example/main.c")[:2], (set(), set()))
        self.assertEqual(self.route("firmware/factory.bin")[2], "firmware")

    def test_cmakelists_is_a_direct_idf_build_input(self) -> None:
        idf, arduino, reason = self.route("examples/esp-idf/01_AXP2101/CMakeLists.txt")
        self.assertEqual((idf, arduino, reason), ({"01_AXP2101"}, set(), "esp-idf-project"))
        self.assertEqual(self.route("examples/esp-idf/01_AXP2101/README.md")[:2], (set(), set()))

    def test_cli_routes_cmakelists_from_a_complete_diff(self) -> None:
        with tempfile.TemporaryDirectory() as temporary:
            repo = Path(temporary)
            project = repo / "examples" / "esp-idf" / "demo"
            project.mkdir(parents=True)
            (project / "CMakeLists.txt").write_text("project(demo)\n", encoding="utf-8")
            subprocess.run(["git", "init", "-q"], cwd=repo, check=True)
            subprocess.run(["git", "add", "."], cwd=repo, check=True)
            subprocess.run(["git", "-c", "user.name=test", "-c", "user.email=test@example.invalid", "commit", "-qm", "base"], cwd=repo, check=True)
            base = subprocess.check_output(["git", "rev-parse", "HEAD"], cwd=repo, text=True).strip()
            (project / "CMakeLists.txt").write_text("project(demo_changed)\n", encoding="utf-8")
            subprocess.run(["git", "add", "."], cwd=repo, check=True)
            subprocess.run(["git", "-c", "user.name=test", "-c", "user.email=test@example.invalid", "commit", "-qm", "change"], cwd=repo, check=True)
            output = repo / "github-output"
            result = subprocess.run([sys.executable, str(ROOT / "scripts" / "ci_routing.py"), "--repo", str(repo), "--base", base, "--github-output", str(output)], text=True, capture_output=True)
            self.assertEqual(result.returncode, 0, result.stderr)
            values = dict(line.split("=", 1) for line in output.read_text(encoding="utf-8").splitlines())
            self.assertEqual(values["esp_idf_count"], "2")
            self.assertEqual(values["arduino_count"], "0")

    def test_rename_delete_and_unknown_are_conservative_or_visible(self) -> None:
        deleted_idf = self.route("examples/esp-idf/removed-example/main/main.c")
        self.assertEqual(len(deleted_idf[0]), 5)
        self.assertEqual(deleted_idf[2], "esp-idf-removed-or-unknown-project")
        deleted_arduino = self.route("examples/arduino/removed-sketch/removed-sketch.ino")
        self.assertEqual(len(deleted_arduino[1]), 7)
        self.assertEqual(deleted_arduino[2], "arduino-removed-or-unknown-sketch")
        renamed = ci_routing.classify(ROOT, ["examples/esp-idf/removed-example/main/main.c", "examples/esp-idf/01_AXP2101/main/main.c"])
        self.assertEqual(renamed["esp_idf_count"], 10)
        result = ci_routing.classify(ROOT, ["unclassified.input"])
        self.assertEqual(result["esp_idf_count"], 10)
        self.assertEqual(result["arduino_count"], 7)
        self.assertEqual(result["unknown_paths"], ["unclassified.input"])

    def test_workflow_uses_the_actual_routing_command_and_gate(self) -> None:
        workflow = (ROOT / ".github/workflows/examples.yml").read_text(encoding="utf-8")
        self.assertIn("python3 scripts/ci_routing.py --base \"$BASE\" --github-output \"$GITHUB_OUTPUT\"", workflow)
        self.assertIn("python3 scripts/markdown_policy.py", workflow)
        self.assertIn("python3 -m unittest discover -s tests -v", workflow)
        self.assertIn("--output-prefix esp_idf_", workflow)
        self.assertIn("--output-prefix arduino_", workflow)
        self.assertIn('"${{ github.ref }}" == refs/tags/*', workflow)
        self.assertIn("cancel-in-progress: true", workflow)

    def test_changed_paths_fail_closed_for_unavailable_or_empty_diff(self) -> None:
        unavailable = SimpleNamespace(returncode=1, stdout="", stderr="missing")
        with patch.object(ci_routing.subprocess, "run", return_value=unavailable):
            with self.assertRaisesRegex(ValueError, "base revision is unavailable"):
                ci_routing.changed_paths(ROOT, "missing-base")
        available = SimpleNamespace(returncode=0, stdout="", stderr="")
        empty = SimpleNamespace(returncode=0, stdout="", stderr="")
        with patch.object(ci_routing.subprocess, "run", side_effect=[available, empty]):
            with self.assertRaisesRegex(ValueError, "diff is empty"):
                ci_routing.changed_paths(ROOT, "main")


if __name__ == "__main__":
    unittest.main()
