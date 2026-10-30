# 固件

[English](README.md) · [首页](../README_ZH.md)

默认固件源码位于 [`brookesia/`](brookesia/)，使用 ESP-IDF 6.1 和 Board Manager，适用于配备 16 MB Flash 和 8 MB PSRAM 的 ESP32-S3-Touch-AMOLED-2.16。

| 文件 | 用途 |
| --- | --- |
| [ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin](ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin) | 最新 Brookesia 桌面、应用、小智 AIChats 和内置 SPIFFS 音频资源 |
| [ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin](ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin) | 之前的出厂恢复镜像 |

最新版为根据最终构建目录 `flash_args` 生成的 16 MB 合并镜像，从 `0x0` 烧录：

```sh
python -m esptool --chip esp32s3 --port <PORT> write-flash 0x0 ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin
```

整包烧录会重置已保存的 Wi-Fi 凭据和设备设置。构建方法及支持的媒体目录见 [Brookesia 固件说明](brookesia/README_ZH.md)。
