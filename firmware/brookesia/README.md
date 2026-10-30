# ESP32-S3-Touch-AMOLED-2.16 firmware

[简体中文](README_ZH.md)

## Features

The 480×480 touch desktop includes Calculator, DrawPanel, SpecAnalyzer, MusicPlayer, Gallery, VideoPlayer, Recorder, Settings and AIChats. Pomodoro, a TXT Reader and Motion (level, horizon, marble and sensors) extend the application set; existing applications include touch-oriented UI improvements.

## Hardware

ESP32-S3-Touch-AMOLED-2.16 with 16 MB Flash and 8 MB PSRAM. Display, touch, power, audio and SD hardware use `esp_board_manager` with the official Waveshare board definition. The S3 keeps software JPEG decoding and small QSPI display buffers.

## Run

Use ESP-IDF 6.1 and run `ESP_BMGR_ASSIST_PREFLIGHT=0 idf.py set-target esp32s3 build flash monitor` from this directory. CMake generates Board Manager descriptors during configuration; no rotary knob or P4 image accelerator is required.

SD media directories are `/music`, `/pictures`, `/videos` and `/books`; the original `/Music`, `/photos`, `/Photos`, `/video`, `/avi` and `/movies` paths remain supported. Recordings go to `/Waveshare/Recordings`. Reader opens TXT files.

## Expected behavior

Startup opens the application desktop. Tap an icon to enter an application and swipe from the bottom for navigation. Pomodoro has touch controls for adjusting time, Motion uses horizontal swipes to change pages, and Reader supports touch page turning.

## Troubleshooting

MusicPlayer plays bundled SPIFFS MP3 tracks without an SD card. Gallery, VideoPlayer, Recorder and Reader show a no-card prompt. AIChats requires Wi-Fi and service activation. Music supports MP3/WAV; video uses MJPEG/PCM AVI.
