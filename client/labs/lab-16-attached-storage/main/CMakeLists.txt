idf_component_register(
    SRCS "app_main.c" "lab16_upload.c"
    INCLUDE_DIRS "."
    REQUIRES esp-box-3 esp_codec_dev esp_driver_sdmmc esp_driver_gpio esp_timer esp_http_client lab16_station freertos mbedtls
)
