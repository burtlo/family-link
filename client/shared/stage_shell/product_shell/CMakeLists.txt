idf_component_register(
    SRCS "product_shell_fw.c"
    INCLUDE_DIRS "include"
    REQUIRES fl_shell_model esp_timer esp_lvgl_port heap freertos
)

if(NOT FAMILY_BUILD_ID)
    message(FATAL_ERROR "Build through make build to supply FAMILY_BUILD_ID")
endif()
target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_BUILD_ID="${FAMILY_BUILD_ID}")
