# 仓库结构

[English](repository-structure.md) · [首页](../README_ZH.md)

这是已规范化的单产品仓库。第一方源代码位于 `examples/esp-idf/` 和
`examples/arduino/`；随附 Arduino 库和嵌入式 Brookesia 上游树不属于产品示例范围。

| 路径 | 用途 |
| --- | --- |
| `examples/esp-idf/` | 5 个直接第一方 ESP-IDF 项目 |
| `examples/arduino/` | 7 个直接 Arduino 草图及随附库 |
| `firmware/` | 不可变的出厂恢复交付物 |
| `config/` | 共享配置和审计策略 |
| `docs/` | 第一方客户和维护者说明 |
| `releases/` | 示例工件打包/下载辅助工具 |

当前布局不存在版本化示例根目录、缺失模板目录或待迁移的本地可复用 BSP 副本。选择性构建路由见
[CI](ci_ZH.md)，保留的本地代码见[组件](components_ZH.md)。
