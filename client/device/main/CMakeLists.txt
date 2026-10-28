idf_component_register(
    SRCS "app_main.c" "product_app_run.c"
           "product_display.c" "product_hardware_buttons.c" "product_network.c"
           "product_mem_probe.c"
           "product_stage_router.cpp"
           "product_login_bridge.cpp" "product_login_http_worker.cpp" "product_inbox_bridge.cpp"
           "product_inbox_http_worker.cpp"
           "product_mic_capture.cpp"
           "product_record_bridge.cpp" "product_send_bridge.cpp" "product_send_http_worker.cpp"
           "product_volume_bridge.cpp"
    INCLUDE_DIRS "."
    REQUIRES product_split_pane fl_product_login fl_product_inbox fl_product_record fl_product_send
             fl_product_volume
             esp-box-3 esp_system
             esp_hw_support driver
             esp_app_format esp_timer
             esp_http_client esp_wifi esp_netif esp_event nvs_flash mbedtls)
if(NOT FIRMWARE_BUILD_ID)
    message(FATAL_ERROR "Build through make build to supply FIRMWARE_BUILD_ID")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIRMWARE_BUILD_ID="${FIRMWARE_BUILD_ID}")
