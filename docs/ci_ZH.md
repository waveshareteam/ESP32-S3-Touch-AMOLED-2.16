# 持续集成

[English](ci.md) · [首页](../README_ZH.md)

始终可见的 `lightweight-gate` 对完整 base/head 差异分类，运行仓库内文档策略和
stdlib 合成测试，并发布所选矩阵。差异为空或 base 不可用时会失败，而不会猜测。

ESP-IDF 发现 `examples/esp-idf/` 下的 5 个直接项目，使用 `v5.5.5` 和 `v6.0.2`。
Arduino 发现 `examples/arduino/` 下的 7 个直接草图，使用核心 `3.3.11`；
`examples/arduino/libraries/**` 不属于产品草图。`workflow_dispatch` 接受 `all`、
项目/草图名称或仓库相对示例路径。

根目录、示例内、草图旁和随附库中的 Markdown 仅运行轻量门禁。直接示例代码只选择
对应示例；共享/全局输入选择适用的完整矩阵。`firmware/` 的变更会报告但不进入示例矩阵。
完整差异中的未知路径会保守地选择全部示例并保持可见。

维护者审计请使用 `config/markdown-audit.json` 和 `config/ci-routing.json` 运行
Waveshare inventory、Markdown 和 routing 工具。仓库内策略脚本是有限的辅助检查，
不能替代完整审计。
