set(BOARD_NAME "esp32_s3_touch_amoled_2_16")

# The board component is fetched through the component manager.  Generate the
# Board Manager descriptor tables while configuring so normal idf.py commands
# do not need a separate board-manager generation step.
set(GENERATED_SRCS "")
if(NOT CMAKE_BUILD_EARLY_EXPANSION)
    idf_component_get_property(BOARD_DEFINITIONS_DIR waveshare_boards COMPONENT_DIR)
    idf_component_get_property(BOARD_MANAGER_DIR espressif__esp_board_manager COMPONENT_DIR)
    set(BOARD_DIR "${BOARD_DEFINITIONS_DIR}/boards/${BOARD_NAME}")
    set(GENERATED_DIR "${CMAKE_BINARY_DIR}/board_manager/components/gen_bmgr_codes")

    file(GLOB BOARD_CONFIG_FILES CONFIGURE_DEPENDS
        "${BOARD_DIR}/*.yaml" "${CMAKE_CURRENT_LIST_DIR}/*.yaml")
    set_property(DIRECTORY APPEND PROPERTY CMAKE_CONFIGURE_DEPENDS ${BOARD_CONFIG_FILES})

    if(NOT EXISTS "${BOARD_DIR}/board_info.yaml")
        message(FATAL_ERROR "Board definition missing: ${BOARD_DIR}")
    endif()

    file(MAKE_DIRECTORY "${CMAKE_BINARY_DIR}/board_manager")
    idf_build_get_property(board_python PYTHON)
    execute_process(
        COMMAND "${board_python}" "${BOARD_MANAGER_DIR}/gen_bmgr_config_codes.py"
                -b "${BOARD_DIR}"
                -a "${CMAKE_CURRENT_LIST_DIR}"
                --project-dir "${CMAKE_BINARY_DIR}/board_manager"
                --skip-sdkconfig-check
                --log-level WARNING
        RESULT_VARIABLE board_gen_result
        OUTPUT_VARIABLE board_gen_output
        ERROR_VARIABLE board_gen_output
    )
    if(NOT board_gen_result EQUAL 0)
        message(FATAL_ERROR "Board Manager code generation failed:\n${board_gen_output}")
    endif()

    file(GLOB GENERATED_SRCS "${GENERATED_DIR}/*.c")
endif()
idf_component_register(
    SRCS "src/bsp_compat.c" ${GENERATED_SRCS}
         "${BOARD_DIR}/setup_device.c" "${BOARD_DIR}/power_manager.c"
    INCLUDE_DIRS "include" "${GENERATED_DIR}" "${BOARD_DIR}"
    REQUIRES esp_board_manager esp_lcd esp_lcd_touch esp_lvgl_adapter
             esp_driver_i2c esp_driver_gpio esp_driver_sdmmc esp_codec_dev
    PRIV_REQUIRES esp_lcd_co5300 esp_lcd_touch_cst9217 spiffs fatfs
)

# The generated device descriptor arrays are linked by name from the board
# manager and must not be discarded by the linker.
idf_component_set_property(${COMPONENT_NAME} WHOLE_ARCHIVE TRUE)
