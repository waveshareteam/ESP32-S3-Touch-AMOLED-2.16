# Firmware

[简体中文](README_ZH.md) · [Home](../README.md)

Factory recovery images for ESP32-S3-Touch-AMOLED-2.16 (ESP32-S3, 16 MB Flash).
Each file is a complete merged image written at `0x0`. Previous versions are retained.

| File | Version date | Purpose | Flash offset |
| --- | --- | --- | --- |
| [ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin](ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin) | 2026-10-30 | Factory recovery | `0x0` |
| [ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin](ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin) | 2026-03-18 | Historical factory recovery image; original filename retained | `0x0` |

Erase the entire Flash before restoring the latest factory image:

```sh
python -m esptool --chip esp32s3 erase-flash
python -m esptool --chip esp32s3 write-flash 0x0 ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin
```

The 2026-10-30 image has SHA-256:
`9cde9793955cb26dcc1d92c83b3090afc5f15f75ee8dcd077e2d0ecd481cd5ac`.
It was renamed to the standard publication filename without changing its contents.
These images are separate factory deliveries, not example CI inputs or outputs.
Source and build instructions are not included here. See [firmware documentation](../docs/firmware.md).
