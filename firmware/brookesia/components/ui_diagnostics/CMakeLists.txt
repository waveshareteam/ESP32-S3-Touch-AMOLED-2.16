idf_component_register(
    SRCS "ui_diagnostics.cpp"
    INCLUDE_DIRS "include"
    REQUIRES brookesia_core espressif__esp_lvgl_adapter esp_driver_usb_serial_jtag
             esp_timer heap bsp_extra VideoPlayer XiaozhiApp
)

target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-missing-field-initializers)
