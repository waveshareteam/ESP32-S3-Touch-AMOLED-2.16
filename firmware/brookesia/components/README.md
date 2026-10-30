# Brookesia firmware components

[简体中文](README_ZH.md)

This directory contains the ESP32-S3-Touch-AMOLED-2.16 product applications
and the small local services needed to coordinate them. It is not a replacement
for the published board support package.

## Applications

| Directory | Function |
| --- | --- |
| [`brookesia_app_calculator/`](brookesia_app_calculator/) | Calculator |
| [`draw/`](draw/) | DrawPanel |
| [`SpecAnalyzer/`](SpecAnalyzer/) | ES7210 microphone spectrum analyzer |
| [`MusicPlayer/`](MusicPlayer/) | MP3/WAV player |
| [`Gallery/`](Gallery/) | SD-card JPEG gallery |
| [`VideoPlayer/`](VideoPlayer/) | MJPEG/PCM AVI player |
| [`Recorder/`](Recorder/) | ES7210 WAV recorder |
| [`Settings/`](Settings/) | Wi-Fi, display, audio, storage, and power status |
| [`Gravitysphere/`](Gravitysphere/) | QMI8658 gravity-ball application |
| [`ButtonTest/`](ButtonTest/) | Target-board button diagnostics |
| [`XiaozhiApp/`](XiaozhiApp/) | Xiaozhi/AI voice and text application |

## Shared services and Registry components

| Directory or dependency | Responsibility |
| --- | --- |
| [`bsp_extra/`](bsp_extra/) | Board-local ES8311 STD TX / ES7210 TDM RX setup and media-player adapter |
| [`storage_service/`](storage_service/) | Shared SD mount access for media applications |
| [`system_status/`](system_status/) | AXP2101 battery/charging status and Wi-Fi status |
| [`rtc_service/`](rtc_service/) | PCF85063A readout, Registry year-base correction, and system-clock synchronization |
| [`chat_history/`](chat_history/) | Optional Xiaozhi text history |
| `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1 | Published BSP: display, touch, GPIO, I2C, and SD card |
| `espressif/esp_codec_dev` 1.5.11 | Codec device layer resolved by the BSP and used by `bsp_extra` |
| `waveshare/qmi8658` 2.0.1 | IMU driver for Gravitysphere |
| `waveshare/pcf85063a` 2.0.0 | RTC access used by the firmware |

AXP2101 has no official online component selected for this project. The local
`system_status` service deliberately keeps its scope to the required register
reads instead of substituting a broader third-party power-management library.

Product documentation identifies the touch controller as CST9220, while BSP
2.0.1 resolves `waveshare/esp_lcd_touch_cst9217` 2.0.0. This dependency graph
compiles, but the controller compatibility is not claimed without HIL.

Registry `waveshare/pcf85063a` 2.0.0 uses a 1970 year base for the RTC's
two-digit year register and does not remove an I2C device when initialization
fails after registration. The local RTC service normalizes the year to the
device's 2000 base and removes that failed-init handle; normal RTC transactions
still use the online driver. The service also reads the raw oscillator-stop bit
that the Registry getter masks and refuses to synchronize an untrusted time.

Registry `waveshare/qmi8658` 2.0.1 has no device-removal API. Gravitysphere
therefore waits for its sensor worker to leave the shared I2C bus naturally and
locally removes the driver-created device handle on initialization failure or
application deinitialization.

Brookesia core and SquareLine remain under
`examples/esp-idf/03_esp-brookesia/components/` and are included through the
project's `EXTRA_COMPONENT_DIRS`; they are not duplicated here. Third-party
trees retain their own licenses and documentation.

Build the full firmware project to validate component integration. A successful
build does not replace on-target peripheral validation.
