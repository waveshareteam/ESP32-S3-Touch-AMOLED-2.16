# Firmware Artifacts

`firmware/` contains factory binary artifacts for user flashing and recovery flows. These binaries are not source projects and are not built by CI.

Source-maintained firmware should live under `examples/esp-idf/`, `examples/arduino/`, or another documented source directory with its own validation path.

CI build outputs are packaged by `releases/package_firmware.py` and uploaded as workflow artifacts. The generated zip contains `manifest.json`, flash helper scripts, flash arguments, and the binaries needed by esptool.

For local release packaging, build the target project first and run the Python script from the repository root. Generated archives are written under `releases/dist/` by default.

Use `releases/download_artifacts.py` to download and extract firmware artifacts from a successful GitHub Actions run into `releases/downloads/`.