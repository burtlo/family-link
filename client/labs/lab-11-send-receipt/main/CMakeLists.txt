idf_component_register(
    SRCS "app_main.c" "lab11_fw.c"
    INCLUDE_DIRS "."
    REQUIRES lab11_send lab11_capture lab11_station esp-box-3 esp_timer esp_lvgl_port esp_http_client freertos
)
