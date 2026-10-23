idf_component_register(
    SRCS "app_main.c" "product_login_bridge.cpp" "product_login_http_worker.cpp"
    INCLUDE_DIRS "."
    REQUIRES product_shell fl_product_login esp-box-3 esp_system esp_hw_support esp_app_format esp_timer
             esp_http_client nvs_flash)
if(NOT FAMILY_BUILD_ID)
    message(FATAL_ERROR "Build through make build to supply FAMILY_BUILD_ID")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_BUILD_ID="${FAMILY_BUILD_ID}")
