# 组件与兼容性

[English](components.md) · [首页](../README_ZH.md)

ESP-IDF 示例使用托管板级组件 `waveshare/esp32_s3_touch_amoled_2_16`，版本范围为
`^2.0.1`。该范围保留为现有 ESP32-S3 示例依赖；只有获得已授权兼容组件发布的证据后才应重新评估。

`XPowersLib`、Brookesia 源码树和本地 app 以及 `bsp_extra` 保持本地：没有语义等价证据允许删除它们。
`bsp_extra` 是频谱分析仪的板级/演示胶水代码，不是已声明的可复用组件。

对仓库原理图进行的只读静态交叉核对显示：第一方板级/示例源与 AMOLED QSPI/reset、触摸
I2C/INT/RST、音频 I2S/PA、BOOT GPIO0 以及 QMI8658 地址的证据一致。托管 BSP 内部的
LCD power/TE、IRQ 和 codec 初始化不在当前 checkout 中，未独立验证。没有修改任何引脚或硬件参数。
