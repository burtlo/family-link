set(LAB02_MODEL "${CMAKE_CURRENT_LIST_DIR}/../../lab-02-bounded-inbox/components/lab02")
idf_component_register(
    SRCS "app_main.c" "media_core.c" "wifi.c" "${LAB02_MODEL}/inbox_model.c"
    INCLUDE_DIRS "." "${CMAKE_BINARY_DIR}" "${LAB02_MODEL}/include"
    REQUIRES json esp_timer esp_http_client esp_wifi esp_netif esp_event nvs_flash
             esp_app_format mbedtls esp_driver_gpio
)
