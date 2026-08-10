# 固件和出厂恢复

[English](firmware.md) · [首页](../README_ZH.md)

`firmware/ESP32-S3-Touch-AMOLED-2.16-FactoryOnly-260318.bin` 是不可变的出厂恢复
交付物。它与 CI 分开清点和报告；常规示例矩阵既不构建也不重新打包它。该出厂镜像的源代码和构建说明
目前未包含在本仓库中，可能在后续更新中提供。

成功的示例 CI 构建会由 `releases/package_firmware.py` 打包为独立诊断工件。归档格式和下载流程请参阅
[发布工具](../releases/README_ZH.md)。
