idf_component_register(
    SRCS "board.c" "wifi.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_timer esp_wifi esp_netif esp_event nvs_flash
)
