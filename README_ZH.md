<div align="center">
  <h1>ESP32-S3-Touch-AMOLED-2.16</h1>
  <p><strong>ESP32-S3 2.16 英寸 480 × 480 QSPI AMOLED 触摸开发板</strong></p>
  <p>
    <a href="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/actions/workflows/examples.yml"><img alt="构建示例" src="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/actions/workflows/examples.yml/badge.svg"></a>
    <a href="https://github.com/waveshareteam/ESP32-S3-Touch-AMOLED-2.16/releases/latest"><img alt="最新发布" src="https://img.shields.io/github/v/release/waveshareteam/ESP32-S3-Touch-AMOLED-2.16"></a>
    <a href="LICENSE"><img alt="许可证" src="https://img.shields.io/github/license/waveshareteam/ESP32-S3-Touch-AMOLED-2.16"></a>
  </p>
  <p><a href="README.md">English</a></p>
  <p>
    <a href="https://www.waveshare.com/esp32-s3-touch-amoled-2.16.htm">🌐 产品页面</a> ·
    <a href="firmware/README_ZH.md">📦 固件</a> ·
    <a href="examples/esp-idf/">🧩 ESP-IDF 示例</a> ·
    <a href="examples/arduino/">🔧 Arduino 示例</a> ·
    <a href="docs/repository-structure_ZH.md">📚 文档</a>
  </p>
</div>

---

## ✨ 概述

这是 ESP32-S3-Touch-AMOLED-2.16 的单产品仓库，提供第一方 ESP-IDF 与
Arduino 示例、出厂恢复固件、原理图和机械尺寸资料。

## 🖥️ 硬件概览

| 项目 | 器件 / 接口 |
| --- | --- |
| MCU | ESP32-S3 |
| 显示 | 2.16 英寸 480 × 480 QSPI AMOLED（CO5300） |
| 触摸 | CST9220 电容触摸控制器 |
| 电源和运动 | AXP2101 和 QMI8658 |
| 音频 | ES7210 ADC 和 ES8311 编解码器 |
| 板级支持 | `waveshare/esp32_s3_touch_amoled_2_16` `^2.0.1` |
| 硬件资料 | [原理图](schematic/) 和 [尺寸](dimensions/) |

## 📦 固件

仓库中的出厂镜像是不可变的恢复交付物，不是示例构建输出。请参阅
[固件和出厂恢复](docs/firmware_ZH.md)；该镜像的源代码和构建说明目前未包含在本仓库中。

## 🧪 示例和工具链

5 个第一方 ESP-IDF 项目使用 ESP-IDF `v5.5.5` 与 `v6.0.2` 测试；7 个
第一方 Arduino 草图使用 Arduino-ESP32 `3.3.11` 测试。随附库中的示例不进入产品矩阵。

- [ESP-IDF 示例](examples/esp-idf/)
- [Arduino 示例](examples/arduino/)
- [持续集成](docs/ci_ZH.md)

## 📚 文档

- [仓库结构](docs/repository-structure_ZH.md)
- [组件与兼容性](docs/components_ZH.md)
- [固件边界](docs/firmware_ZH.md)
- [Brookesia 说明](docs/brookesia_ZH.md)
- [发布工具](releases/README_ZH.md)

## 🗂️ 仓库布局

`examples/` 保存第一方源代码项目；`firmware/` 保存独立的出厂交付物；
`releases/` 保存打包辅助工具。CI 范围和文档所有权见[结构指南](docs/repository-structure_ZH.md)。

## 🤝 支持与贡献

产品支持请参阅 [SUPPORT_ZH.md](SUPPORT_ZH.md)，贡献要求请参阅
[CONTRIBUTING_ZH.md](CONTRIBUTING_ZH.md)。本仓库没有经核实的私密漏洞报告渠道，因此不在此宣传安全报告方式。

## 📄 许可证

本仓库采用 Apache License 2.0，见 [LICENSE](LICENSE)。
