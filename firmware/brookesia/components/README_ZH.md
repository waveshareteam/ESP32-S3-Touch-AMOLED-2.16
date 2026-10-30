# Brookesia 固件组件

[English](README.md)

本目录包含 ESP32-S3-Touch-AMOLED-2.16 的产品应用，以及协调这些应用所需的少量
本地服务；它不替代已发布的开发板 BSP。

## 应用

| 目录 | 功能 |
| --- | --- |
| [`brookesia_app_calculator/`](brookesia_app_calculator/) | Calculator（计算器） |
| [`draw/`](draw/) | DrawPanel（画板） |
| [`SpecAnalyzer/`](SpecAnalyzer/) | ES7210 麦克风频谱分析仪 |
| [`MusicPlayer/`](MusicPlayer/) | MP3/WAV 播放器 |
| [`Gallery/`](Gallery/) | SD 卡 JPEG 图库 |
| [`VideoPlayer/`](VideoPlayer/) | MJPEG/PCM AVI 播放器 |
| [`Recorder/`](Recorder/) | ES7210 WAV 录音机 |
| [`Settings/`](Settings/) | Wi-Fi、显示、音频、存储和电源状态 |
| [`Gravitysphere/`](Gravitysphere/) | QMI8658 重力球应用 |
| [`ButtonTest/`](ButtonTest/) | 目标板按键诊断 |
| [`XiaozhiApp/`](XiaozhiApp/) | Xiaozhi/AI 语音与文本应用 |

## 公共服务与注册表组件

| 目录或依赖 | 职责 |
| --- | --- |
| [`bsp_extra/`](bsp_extra/) | 本板 ES8311 STD 输出 / ES7210 TDM 输入初始化与媒体播放器适配 |
| [`storage_service/`](storage_service/) | 媒体应用共享的 SD 卡挂载访问 |
| [`system_status/`](system_status/) | AXP2101 电池/充电状态与 Wi-Fi 状态 |
| [`rtc_service/`](rtc_service/) | PCF85063A 读取、Registry 年份基准校正与系统时钟同步 |
| [`chat_history/`](chat_history/) | 可选的 Xiaozhi 文本历史记录 |
| `waveshare/esp32_s3_touch_amoled_2_16` 2.0.1 | 已发布 BSP：显示、触摸、GPIO、I2C 和 SD 卡 |
| `espressif/esp_codec_dev` 1.5.11 | 由 BSP 解析、供 `bsp_extra` 使用的编解码器设备层 |
| `waveshare/qmi8658` 2.0.1 | Gravitysphere 使用的 IMU 驱动 |
| `waveshare/pcf85063a` 2.0.0 | 固件使用的 RTC 访问 |

本工程没有选用 AXP2101 官方在线组件。为了避免引入更宽泛的第三方电源管理库，本地
`system_status` 服务刻意只保留所需的轻量寄存器读取。

产品文档标注触摸控制器为 CST9220，而 BSP 2.0.1 解析到
`waveshare/esp_lcd_touch_cst9217` 2.0.0。该依赖组合已编译通过，但没有 HIL 时不声明
控制器运行时兼容。

Registry `waveshare/pcf85063a` 2.0.0 对 RTC 两位年份寄存器使用 1970 基准，并且在
注册 I2C device 后初始化失败时不会移除该 device。本地 RTC 服务会将年份校正为器件
采用的 2000 年基准，并清理失败初始化遗留的句柄；正常 RTC 事务仍由在线驱动执行。
服务还会读取 Registry getter 所屏蔽的原始振荡器停止位，拒绝同步不可信时间。

Registry `waveshare/qmi8658` 2.0.1 没有 device 移除 API。Gravitysphere 会等待传感器
任务自然离开共享 I2C 总线，并在初始化失败或应用反初始化时局部移除驱动创建的句柄。

Brookesia 核心与 SquareLine 位于
`examples/esp-idf/03_esp-brookesia/components/`，并通过工程的
`EXTRA_COMPONENT_DIRS` 引入，不会在本目录重复存放。第三方代码保留各自的许可证
和文档。

请构建完整固件工程以验证组件集成；构建成功不能替代目标板外设验证。
