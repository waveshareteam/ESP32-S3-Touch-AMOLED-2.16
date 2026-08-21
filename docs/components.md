# Components and compatibility

[简体中文](components_ZH.md) · [Home](../README.md)

ESP-IDF examples use the managed board component
`waveshare/esp32_s3_touch_amoled_2_16` at `^2.0.1`. This range is retained
because it is the existing board dependency for the ESP32-S3 examples; revisit
it only with evidence from an authorized compatible component release.

`XPowersLib`, the Brookesia source tree and local app, and `bsp_extra` remain
local: no semantic-equivalence evidence authorizes their removal. `bsp_extra`
is spectrum-analyzer board/demo glue rather than a declared reusable component.

Schematic page 1 corroborates the maintained display QSPI pins (GPIO4/5/6/7,
GPIO38, GPIO12, and reset GPIO39), shared I2C (SDA GPIO15 and SCL GPIO14), the
QMI8658 I2C address (0x6B), audio pins (MCLK GPIO42, SCLK GPIO9, LRCK GPIO45,
and DOUT GPIO8), and BOOT GPIO0. Managed-BSP internals, touch address, SD, and
USB remain outside this independent verification. The malformed AXP2101
`sdkconfig.defaults` entries were corrected to the existing schematic-backed
SDA GPIO15/SCL GPIO14 values; no schematic pin assignment was changed.
