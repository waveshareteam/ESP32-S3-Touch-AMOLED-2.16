idf_component_register(
    SRCS "Motion.cpp" "assets/img_app_motion.c"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core esp32_s3_touch_amoled_2_16 qmi8658 nvs_flash esp_timer
    WHOLE_ARCHIVE
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
