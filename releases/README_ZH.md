# 发布工具

[English](README.md) · [首页](../README_ZH.md)

`package_firmware.py` 将成功的示例构建打包为可烧录工作流工件；它不会修改仓库中的出厂镜像。
`download_artifacts.py` 下载已发布的 GitHub Actions 工件。生成的归档应位于忽略的输出目录，
且不是出厂恢复交付物。

CI 矩阵使用 ESP-IDF `v5.5.5`/`v6.0.2` 和 Arduino-ESP32 `3.3.11`。请参阅
[CI 路由](../docs/ci_ZH.md)和[固件边界](../docs/firmware_ZH.md)。
