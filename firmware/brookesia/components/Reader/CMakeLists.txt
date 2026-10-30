idf_component_register(
    SRCS "Reader.cpp" "page_layout.cpp" "text_source.c" "assets/img_app_reader.c"
    INCLUDE_DIRS "."
    REQUIRES brookesia_core storage_service app_fonts text_codec nvs_flash
)
