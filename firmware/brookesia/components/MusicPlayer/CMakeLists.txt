idf_component_register(
    SRCS "MusicPlayer.cpp" "music_audio.c" "music_meta.cpp" "assets/img_app_musicplayer.c"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core bsp_extra storage_service player_ui text_codec app_fonts esp_new_jpeg heap esp_timer chmorgan__esp-audio-player esp32_s3_touch_amoled_2_16
)
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
