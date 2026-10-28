idf_component_register(
    SRCS "product_shell_fw.c"
    INCLUDE_DIRS "include"
    REQUIRES fl_shell_model esp_timer esp_lvgl_port heap freertos
)

if(NOT FIRMWARE_BUILD_ID)
    message(FATAL_ERROR "Build through make build to supply FIRMWARE_BUILD_ID")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIRMWARE_BUILD_ID="${FIRMWARE_BUILD_ID}")
