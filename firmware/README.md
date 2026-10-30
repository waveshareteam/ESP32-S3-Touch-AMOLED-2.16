# Firmware

[简体中文](README_ZH.md) · [Home](../README.md)

The default firmware source is in [`brookesia/`](brookesia/), built with ESP-IDF 6.1 and Board Manager for the ESP32-S3-Touch-AMOLED-2.16 with 16 MB flash and 8 MB PSRAM.

| File | Purpose |
| --- | --- |
| [ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin](ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin) | Latest Brookesia desktop, applications, AIChats and bundled SPIFFS audio resources |
| [ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin](ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin) | Previous factory recovery image |

The latest file is a 16 MB combined image generated from the final build's `flash_args`. Flash it at `0x0`:

```sh
python -m esptool --chip esp32s3 --port <PORT> write-flash 0x0 ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin
```

Flashing the complete image resets saved Wi-Fi credentials and device settings. See the [Brookesia firmware guide](brookesia/README.md) for building and supported media directories.
