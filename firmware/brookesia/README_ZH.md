# ESP32-S3-Touch-AMOLED-2.16 固件

[English](README.md)

## 功能

480×480 触摸桌面包含计算器、画板、频谱分析、音乐、图库、视频、录音、设置和 AIChats。新增番茄钟、TXT 阅读器和 Motion（水平仪、姿态、重力球、传感器），原有应用同步适合触摸操作的界面改进。

## 硬件需求

ESP32-S3-Touch-AMOLED-2.16，16 MB Flash、8 MB PSRAM。板级显示、触摸、电源、音频和 SD 卡通过 `esp_board_manager` 与 Waveshare 官方板定义接入；S3 保留软件 JPEG 解码和小块 QSPI 显示缓冲。

## 怎么运行

使用 ESP-IDF 6.1，在本目录执行 `ESP_BMGR_ASSIST_PREFLIGHT=0 idf.py set-target esp32s3 build flash monitor`。板管理器描述表由 CMake 在构建时生成；工程不需要旋钮或 P4 的硬件图像加速。

SD 卡媒体目录为 `/music`、`/pictures`、`/videos`、`/books`；同时兼容原有 `/Music`、`/photos`、`/Photos`、`/video`、`/avi`、`/movies` 等目录。录音保存到 `/Waveshare/Recordings`。阅读器打开 TXT 文本。

## 预期现象

启动后显示应用桌面；点击图标进入应用，底部手势呼出导航。番茄钟通过触摸加减调整时间，Motion 左右滑动换页，阅读器支持触摸翻页。

## 常见问题

无 SD 卡时 MusicPlayer 仍可播放 SPIFFS 内置 MP3；图库、视频、录音与阅读器显示无卡提示。AIChats 需要先在设置中连接 Wi-Fi 并完成服务激活；音乐支持 MP3/WAV，视频使用 MJPEG/PCM AVI。
