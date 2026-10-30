# Firmware and factory recovery

[简体中文](firmware_ZH.md) · [Home](../README.md)

The default factory firmware source is [firmware/brookesia](../firmware/brookesia/), using ESP-IDF 6.1 and the official Waveshare Board Manager definition. See the [firmware downloads and flashing instructions](../firmware/README.md) and [source build guide](../firmware/brookesia/README.md).

The latest combined image is `ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin`, generated from the final build's `flash_args` and flashed at `0x0`. Previous factory recovery images remain available. The normal example matrix does not build or repackage this factory image.

Successful example CI builds are separate diagnostic artifacts packaged by `releases/package_firmware.py`. See [release tools](../releases/README.md) for the archive format and download flow.
