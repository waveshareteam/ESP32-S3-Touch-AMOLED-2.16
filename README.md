<div align="center">
  <h1>ESP32-S3-Touch-AMOLED-2.16</h1>
  <p><strong>ESP32-S3 2.16-inch 480 x 480 QSPI AMOLED touch development board</strong></p>
  <p>
    <a href="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/actions/workflows/examples.yml"><img alt="Build Examples" src="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/actions/workflows/examples.yml/badge.svg"></a>
    <a href="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/releases/latest"><img alt="Latest Release" src="https://img.shields.io/github/v/release/waveshareteam/ESP32-S3-Touch-AMOLED-2.16"></a>
    <a href="LICENSE"><img alt="License" src="https://img.shields.io/github/license/waveshareteam/ESP32-S3-Touch-AMOLED-2.16"></a>
  </p>
  <p>
    <a href="https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm">🌐 Product Page</a> ·
    <a href="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/releases/latest">📦 Firmware Releases</a> ·
    <a href="examples/esp-idf/">🧩 ESP-IDF Examples</a> ·
    <a href="examples/arduino/">🔧 Arduino Examples</a> ·
    <a href="docs/">📚 Documentation</a>
  </p>
</div>

---

## ✨ Overview

This repository provides example software, source-built firmware packages,
factory recovery firmware, schematics, and mechanical references for the
Waveshare ESP32-S3-Touch-AMOLED-2.16.

The board combines an ESP32-S3 with a square AMOLED display, capacitive touch,
motion sensing, power management, and audio interfaces in a compact
watch-style development platform.

## 🖥️ Hardware Overview

| Feature | Device / interface |
| --- | --- |
| MCU | ESP32-S3 |
| Display | 2.16-inch 480 x 480 QSPI AMOLED using CO5300 |
| Touch | CST9220 capacitive touch controller using the CST92xx driver |
| Power management | AXP2101 |
| Motion sensor | QMI8658 six-axis IMU |
| Audio | Dual onboard microphones with ES7210 ADC and ES8311 codec |
| Board support | Managed component: `waveshare/esp32_s3_touch_amoled_2_16` |
| Hardware files | [Schematics](schematic/) and [mechanical dimensions](dimensions/) |

## 📦 Firmware Releases

The fastest way to try an example is to use a ready-to-flash package from the
[latest release](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/releases/latest).

1. Download the `*-combined.zip` package for the example and framework version
   you need.
2. Extract the archive and install esptool with
   `python -m pip install esptool`.
3. Connect the board over USB.
4. Run `flash_combined.bat COMx` on Windows or
   `./flash_combined.sh /dev/ttyACM0` on Linux.
5. Reset the board if it does not restart automatically.

> [!NOTE]
> Combined images are flashed at offset `0x0`. Each package also contains the
> original split binaries, flash arguments, helper scripts, and checksums.

Factory recovery images under [firmware](firmware/) are separate from
CI-generated example firmware. See
[Firmware and Factory Recovery](docs/firmware.md) for details.

## 🧪 Examples

### ESP-IDF

| Example | Focus |
| --- | --- |
| [01_AXP2101](examples/esp-idf/01_AXP2101/) | Power management and battery telemetry |
| [02_lvgl_demo_v9](examples/esp-idf/02_lvgl_demo_v9/) | LVGL 9 display benchmark |
| [03_esp-brookesia](examples/esp-idf/03_esp-brookesia/) | ESP-Brookesia application UI |
| [04_Immersive_block](examples/esp-idf/04_Immersive_block/) | Motion-driven LVGL block demo |
| [05_Spec_Analyzer](examples/esp-idf/05_Spec_Analyzer/) | Microphone spectrum analyzer |

### Arduino

| Example | Focus |
| --- | --- |
| [01_HelloWorld](examples/arduino/01_HelloWorld/) | Display bring-up |
| [02_GFX_AsciiTable](examples/arduino/02_GFX_AsciiTable/) | GFX text and character rendering |
| [03_LVGL_AXP2101_ADC_Data](examples/arduino/03_LVGL_AXP2101_ADC_Data/) | LVGL power telemetry UI |
| [04_LVGL_QMI8658_ui](examples/arduino/04_LVGL_QMI8658_ui/) | LVGL IMU data UI |
| [05_LVGL_Widgets](examples/arduino/05_LVGL_Widgets/) | LVGL music UI, touch input, and IMU-based rotation |
| [06_ES7210](examples/arduino/06_ES7210/) | ES7210 microphone input |
| [07_ES8311](examples/arduino/07_ES8311/) | ES8311 audio output |

Bundled Arduino libraries live under
[`examples/arduino/libraries`](examples/arduino/libraries/). Their upstream
library examples are intentionally excluded from the product CI matrix.

## 🛠️ Supported Toolchains

| Surface | Version | Firmware builds |
| --- | --- | ---: |
| ESP-IDF | `v5.5.4` | 5 |
| ESP-IDF | `v6.0.2` | 5 |
| Arduino-ESP32 | `3.3.10` | 7 |

The [Build Examples workflow](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/actions/workflows/examples.yml)
runs two discovery jobs and 17 firmware build jobs. Each successful build is
packaged as a flashable firmware artifact. See [Continuous Integration](docs/ci.md)
for matrix and dispatch details.

## 🗂️ Repository Layout

| Path | Purpose |
| --- | --- |
| [`examples/esp-idf/`](examples/esp-idf/) | First-party ESP-IDF projects |
| [`examples/arduino/`](examples/arduino/) | First-party Arduino sketches and bundled libraries |
| [`firmware/`](firmware/) | Factory flashing and recovery binaries |
| [`releases/`](releases/) | Packaging, artifact download, and release tools |
| [`schematic/`](schematic/) | Public schematic files |
| [`dimensions/`](dimensions/) | Mechanical reference files |
| [`config/`](config/) | Shared ESP-IDF configuration overlays |
| [`docs/`](docs/) | Repository, CI, component, and firmware notes |

## 📚 Documentation

- [Repository Structure](docs/repository-structure.md)
- [Continuous Integration](docs/ci.md)
- [Components](docs/components.md)
- [Firmware and Factory Recovery](docs/firmware.md)
- [ESP-Brookesia Notes](docs/brookesia.md)
- [Release Tools](releases/README.md)

## 🤝 Support and Contributions

Contributions and reproducible issue reports are welcome. Include the example
path, framework version, reproduction steps, expected behavior, actual
behavior, and relevant serial logs.

- [Contributing Guide](CONTRIBUTING.md)
- [Support](SUPPORT.md)
- [Security Policy](SECURITY.md)
- [Open an Issue](https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/issues/new/choose)

## 📄 License

This repository is licensed under the Apache License 2.0. See
[LICENSE](LICENSE).
