idf_component_register(
    SRCS "lab11_station.c"
    INCLUDE_DIRS "include"
    REQUIRES esp_netif esp_event esp_wifi nvs_flash
)
