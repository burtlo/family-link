idf_component_register(SRCS "app_main.c" "lab12_ui.c" "lab12_fixture_client.c"
    EMBED_TXTFILES "../fixtures/layout.json"
    INCLUDE_DIRS "."
    REQUIRES lab12_model esp-box-3 esp_lvgl_port esp_wifi esp_event esp_netif nvs_flash esp_http_client json freertos)
