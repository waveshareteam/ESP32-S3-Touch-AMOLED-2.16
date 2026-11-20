# 固件

[English](README.md) · [首页](../README_ZH.md)

适用于 ESP32-S3-Touch-AMOLED-2.16（ESP32-S3，16 MB Flash）的出厂恢复固件。
每个文件都是从 `0x0` 烧录的完整合并镜像，历史版本保留。

| 文件 | 版本日期 | 用途 | 烧录偏移 |
| --- | --- | --- | --- |
| [ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin](ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin) | 2026-10-30 | 出厂恢复固件 | `0x0` |
| [ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20260318.bin](ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20260318.bin) | 2026-03-18 | 历史出厂恢复固件 | `0x0` |

恢复最新出厂固件前，先整片擦除 Flash：

```sh
python -m esptool --chip esp32s3 erase-flash
python -m esptool --chip esp32s3 write-flash 0x0 ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin
```

2026-10-30 镜像的 SHA-256：
`9cde9793955cb26dcc1d92c83b3090afc5f15f75ee8dcd077e2d0ecd481cd5ac`。
本次仅按发布规范重命名，BIN 内容未变。
这些镜像属于独立的出厂交付，不是示例 CI 的输入或输出。
此处未包含源代码和构建说明，请参阅[固件文档](../docs/firmware_ZH.md)。
