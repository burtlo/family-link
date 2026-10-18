idf_component_register(
    SRCS "app_main.c" "lab10_ui_lvgl.c"
    INCLUDE_DIRS "."
    REQUIRES esp-box-3 esp_timer esp_driver_gpio esp_lvgl_port heap lab10_audio lab10_capture lab10_render lab10_ui lab10_workload
)
