set(LAB02_SRCS
    lab_main.c
    lab02_session.c
    inbox_model.c
    inbox_page_metadata.c
    page_provider.c
    page_provider_local.c
    page_provider_http.c
    carousel_controller.c
    card_pool.c
    card_view.c
    card_view_layout.c
    snap_math.c
    snap.c
    instrumentation.c
    trace_emit.c
    lab_harness.c
)

if(CONFIG_LAB02_EAGER_BASELINE)
    list(APPEND LAB02_SRCS eager_carousel.c)
endif()

idf_component_register(
    SRCS ${LAB02_SRCS}
    INCLUDE_DIRS "include"
    REQUIRES lab02_platform lab02_fixtures json esp_timer nvs_flash lvgl__lvgl
    PRIV_REQUIRES esp_app_format esp_http_client
)
