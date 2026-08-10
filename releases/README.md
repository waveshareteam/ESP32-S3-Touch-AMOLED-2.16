# Release tools

[简体中文](README_ZH.md) · [Home](../README.md)

`package_firmware.py` packages successful example builds into flashable workflow
artifacts; it does not modify the checked-in factory image. `download_artifacts.py`
downloads published GitHub Actions artifacts. Generated archives belong in ignored
output directories and are not factory recovery deliveries.

The CI matrix uses ESP-IDF `v5.5.5`/`v6.0.2` and Arduino-ESP32 `3.3.11`. See
[CI routing](../docs/ci.md) and [firmware boundary](../docs/firmware.md).
