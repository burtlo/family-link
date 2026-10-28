idf_component_register(
    SRCS "product_split_pane.c"
    INCLUDE_DIRS "include"
    REQUIRES fl_shell_model esp_lvgl_port
)

if(NOT FIRMWARE_BUILD_ID)
    message(FATAL_ERROR "Build through make build to supply FIRMWARE_BUILD_ID")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE FIRMWARE_BUILD_ID="${FIRMWARE_BUILD_ID}")
