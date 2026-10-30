idf_component_register(
    SRCS "rtc_service.c"
    INCLUDE_DIRS "include"
    REQUIRES
        esp32_s3_touch_amoled_2_16
        waveshare__pcf85063a
)
