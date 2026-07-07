# ESP32-S3-Touch-AMOLED-2.16

Sample programs, firmware recovery artifacts, and hardware references for the
Waveshare ESP32-S3-Touch-AMOLED-2.16 development board.

The board is an ESP32-S3 based 2.16 inch AMOLED touch watch development board
with a 480 x 480 QSPI display and onboard digital microphones.

## Repository Layout

```text
examples/esp-idf/      ESP-IDF first-party examples
examples/arduino/      Arduino first-party sketches and bundled libraries
firmware/              Factory flashing and recovery binaries
schematic/             Public schematic files
dimensions/            Mechanical reference files
config/                Shared ESP-IDF configuration overlays
docs/                  Repository, CI, component, and firmware notes
```

Historical versioned example roots were removed. Use the canonical example paths for new work.

## Examples

ESP-IDF projects are under `examples/esp-idf/`:

- `01_AXP2101`
- `02_lvgl_demo_v9`
- `03_esp-brookesia`
- `04_Immersive_block`
- `05_Spec_Analyzer`

Arduino sketches are under `examples/arduino/`:

- `01_HelloWorld`
- `02_GFX_AsciiTable`
- `03_LVGL_AXP2101_ADC_Data`
- `04_LVGL_QMI8658_ui`
- `05_LVGL_Widgets`
- `06_ES7210`
- `07_ES8311`

Bundled Arduino libraries are kept in `examples/arduino/libraries/` and are
used by the product sketches. Their own library examples are not part of the
default product CI matrix.

## Continuous Integration

GitHub Actions builds first-party examples only:

- ESP-IDF examples with ESP-IDF `v5.5.4` and `v6.0.2`
- Arduino sketches with Arduino-ESP32 `3.3.10`

See `docs/ci.md` for the matrix, dispatch inputs, and artifact behavior.

## Firmware

Factory binaries in `firmware/` are checked-in recovery artifacts, not CI build
outputs. CI-generated ESP-IDF and Arduino firmware packages are uploaded as workflow
artifacts and are not committed to the repository.

See `docs/firmware.md` and `firmware/README.md` for details.

## Documentation

- `docs/repository-structure.md`
- `docs/ci.md`
- `docs/components.md`
- `docs/firmware.md`
- `docs/brookesia.md`

## Contributing and Support

Contributions and issue reports are welcome. Please read:

- `CONTRIBUTING.md`
- `SUPPORT.md`
- `SECURITY.md`

## License

This repository is licensed under the Apache License 2.0. See `LICENSE` for
details.
