idf_component_register(
    SRCS "app_main.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-box-3 esp_timer esp_lvgl_port heap lab04_format
    EMBED_FILES "../fixtures/corpus-b.csv" "../fixtures/short.sketch" "../fixtures/dense.sketch"
)
