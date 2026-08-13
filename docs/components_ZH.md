# 组件与兼容性

[English](components.md) · [首页](../README_ZH.md)

ESP-IDF 示例使用托管板级组件 `waveshare/esp32_s3_touch_amoled_2_16`，版本范围为
`^2.0.1`。该范围保留为现有 ESP32-S3 示例依赖；只有获得已授权兼容组件发布的证据后才应重新评估。

`XPowersLib`、Brookesia 源码树和本地 app 以及 `bsp_extra` 保持本地：没有语义等价证据允许删除它们。
`bsp_extra` 是频谱分析仪的板级/演示胶水代码，不是已声明的可复用组件。

原理图第 1 页佐证了维护中的显示 QSPI 引脚（GPIO4/5/6/7、GPIO38、GPIO12 和 reset GPIO39）、
共享 I2C（SDA GPIO15、SCL GPIO14）、QMI8658 I2C 地址（0x6B）、音频引脚（MCLK GPIO42、
SCLK GPIO9、LRCK GPIO45、DOUT GPIO8）以及 BOOT GPIO0。托管 BSP 内部、触摸地址、SD 和
USB 仍未独立验证。AXP2101 的 `sdkconfig.defaults` 原有格式错误已按原理图所证实的
SDA GPIO15/SCL GPIO14 修正；原理图引脚分配没有改变。
