# 固件和出厂恢复

[English](firmware.md) · [首页](../README_ZH.md)

默认出厂固件源码位于 [firmware/brookesia](../firmware/brookesia/)，使用 ESP-IDF 6.1 和微雪官方 Board Manager 板定义。[固件下载及烧录说明](../firmware/README_ZH.md)和[源码构建指南](../firmware/brookesia/README_ZH.md)也在仓库中。

最新合并镜像为 `ESP32-S3-Touch-AMOLED-2.16_FactoryOnly_20261030.bin`，根据最终构建目录的 `flash_args` 生成，从 `0x0` 烧录。历史出厂恢复镜像继续保留。常规示例矩阵不会构建或重新打包这个出厂镜像。

成功的示例 CI 构建会由 `releases/package_firmware.py` 打包为独立诊断工件。归档格式和下载流程请参阅[发布工具](../releases/README_ZH.md)。
