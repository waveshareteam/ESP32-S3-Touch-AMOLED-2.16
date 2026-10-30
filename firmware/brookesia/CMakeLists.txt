cmake_minimum_required(VERSION 3.16)

# Reuse the board-tested Brookesia core and SquareLine demo from the ESP-IDF
# example.  The firmware keeps only its product-specific applications here so
# the large framework sources are not duplicated in the repository.
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../../examples/esp-idf/03_esp-brookesia/components/brookesia_core"
    "${CMAKE_CURRENT_LIST_DIR}/../../examples/esp-idf/03_esp-brookesia/components/brookesia_app_squareline_demo"
)

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(esp32-s3-touch-amoled-2-16-brookesia)

# mcp-c-sdk 2.0.1 uses bounded strncpy calls that GCC 14 cannot prove are
# terminated. Keep the Registry component unchanged and relax only this
# warning for its own target; all other warnings remain errors.
if(TARGET __idf_espressif__mcp-c-sdk AND CMAKE_C_COMPILER_ID STREQUAL "GNU")
    target_compile_options(__idf_espressif__mcp-c-sdk PRIVATE
        -Wno-error=stringop-truncation
    )
endif()

# esp-boost 0.3.x Proto sources require C++23 on the IDF 6.1 compiler.
if(TARGET __idf_espressif__esp-boost)
    target_compile_options(__idf_espressif__esp-boost PRIVATE
        "$<$<COMPILE_LANGUAGE:CXX>:-std=gnu++23>")
endif()
