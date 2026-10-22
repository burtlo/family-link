idf_component_register(
    SRCS "app_main.cpp" "desk_station.cpp"
    INCLUDE_DIRS "."
    REQUIRES esp-box-3 esp_timer esp_http_client nvs_flash esp_netif esp_event esp_wifi heap lab08_pin lab07_session
)
