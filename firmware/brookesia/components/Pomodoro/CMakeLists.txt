idf_component_register(
    SRCS "Pomodoro.cpp" "pomodoro_timer.c" "assets/img_app_pomodoro.c"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core app_fonts esp_timer nvs_flash
)
