# Components

This repository uses ESP-IDF Component Manager for the shared Waveshare board
support package and keeps only project-specific glue or demo framework code in
source control.

## Local Components

Local project components currently include:

- `examples/esp-idf/01_AXP2101/components/XPowersLib`
- `examples/esp-idf/03_esp-brookesia/components/brookesia_app_squareline_demo`
- `examples/esp-idf/03_esp-brookesia/components/brookesia_core`
- `examples/esp-idf/05_Spec_Analyzer/components/bsp_extra`

`bsp_extra` is project glue for the spectrum analyzer example and should remain
local unless it becomes reusable across boards. Brookesia remains source-local
until a verified shared component path is available.

## Managed BSP

The repeated local BSP copies were removed. ESP-IDF examples now resolve the
board package from the ESP Component Registry:

```text
waveshare/esp32_s3_touch_amoled_2_16
version: ^2.0.1
target: esp32s3
idf: >=5.5
```

Use the registry component for display, touch, audio, SD, and shared board APIs.
If a CI failure is rooted in the managed BSP, fix and release the shared
component first, then update this repository dependency range.