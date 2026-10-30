idf_component_register(
    SRCS "now_playing_view.cpp"
    INCLUDE_DIRS "include"
    REQUIRES lvgl app_fonts esp32_s3_touch_amoled_2_16
    PRIV_REQUIRES esp_new_jpeg heap)
