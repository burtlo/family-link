idf_component_register(
    SRCS "app_main.c" "lab06_fw.c"
    INCLUDE_DIRS "."
    REQUIRES lab06_model esp-box-3 esp_timer esp_lvgl_port heap freertos
)
