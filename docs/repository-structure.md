# Repository Structure

This repository is maintained as a product repository for
ESP32-S3-Touch-AMOLED-2.16.

## Current Shape

Inventory classified the repository as:

- Versioned or inconsistent example roots
- Local reusable component copies
- Brookesia or rich UI firmware
- Arduino sketches with bundled libraries
- Factory binaries or firmware artifacts
- Public collaboration templates missing

The board target for first-party examples is `esp32s3`. P4/C6 hosted Wi-Fi
files appear only inside bundled third-party library examples and are excluded
from product CI.

## Canonical Layout

```text
examples/esp-idf/      ESP-IDF first-party projects
examples/arduino/      Arduino first-party sketches and bundled libraries
firmware/              Factory flashing and recovery binaries
schematic/             Hardware schematic files
dimensions/            Mechanical reference files
config/                Shared ESP-IDF configuration overlays
docs/                  Repository, CI, component, and firmware notes
```

Historical versioned roots were removed during layout normalization. New examples should be added under the canonical paths.

## First-Party CI Scope

ESP-IDF CI discovers only direct projects under `examples/esp-idf/` that contain
`CMakeLists.txt` and `main/`.

Arduino CI discovers sketches under `examples/arduino/` while excluding
`examples/arduino/libraries/**`. Bundled library examples are useful upstream
library examples, but they are not product examples for this repository.
