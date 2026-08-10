# Components and compatibility

[简体中文](components_ZH.md) · [Home](../README.md)

ESP-IDF examples use the managed board component
`waveshare/esp32_s3_touch_amoled_2_16` at `^2.0.1`. This range is retained
because it is the existing board dependency for the ESP32-S3 examples; revisit
it only with evidence from an authorized compatible component release.

`XPowersLib`, the Brookesia source tree and local app, and `bsp_extra` remain
local: no semantic-equivalence evidence authorizes their removal. `bsp_extra`
is spectrum-analyzer board/demo glue rather than a declared reusable component.

A read-only static cross-check against the repository schematic found consistent
AMOLED QSPI/reset, touch I2C/INT/RST, audio I2S/PA, BOOT GPIO0, and QMI8658
address evidence in the first-party board/example sources. Managed-BSP internal
LCD power/TE, IRQ, and codec initialization are not present in this checkout and
were not independently verified. No pin or hardware parameter was changed.
