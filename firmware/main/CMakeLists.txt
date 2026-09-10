if(NOT DEFINED FAMILY_DEMO OR FAMILY_DEMO STREQUAL "")
    set(FAMILY_DEMO "h02_display_count")
endif()

set(DEMO_SRC "${CMAKE_CURRENT_LIST_DIR}/../demos/${FAMILY_DEMO}.c")
if(FAMILY_DEMO STREQUAL "x02_product_shell")
    set(V1_SRCS
        "${CMAKE_CURRENT_LIST_DIR}/../v1/x02_main.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_api.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_ui_common.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_state.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_connect.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_auth.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_carousel.c"
        "${CMAKE_CURRENT_LIST_DIR}/../v1/v1_record.c"
    )
    set(V1_INCLUDE "${CMAKE_CURRENT_LIST_DIR}/../v1")
else()
    set(V1_SRCS "")
    set(V1_INCLUDE "")
endif()
if(NOT EXISTS "${DEMO_SRC}")
    message(FATAL_ERROR "Unknown FAMILY_DEMO=${FAMILY_DEMO} (expected ${DEMO_SRC})")
endif()

set(SECRETS "${CMAKE_CURRENT_LIST_DIR}/../secrets.h")
if(NOT EXISTS "${SECRETS}")
    set(SECRETS "${CMAKE_CURRENT_LIST_DIR}/../secrets.example.h")
endif()

idf_component_register(
    SRCS
        "${DEMO_SRC}"
        ${V1_SRCS}
        "${CMAKE_CURRENT_LIST_DIR}/../common/pass.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/board.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/wifi_sta.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/http_bearer.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/who.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../common"
        "${CMAKE_CURRENT_LIST_DIR}/.."
        ${V1_INCLUDE}
    PRIV_REQUIRES
        esp_timer
        driver
        nvs_flash
        esp_wifi
        esp_event
        esp_http_client
        esp_netif
        json
        mbedtls
        esp_websocket_client
)

# secrets.h is gitignored; demos that need SSID/PIN include it.
target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_DEMO_NAME="${FAMILY_DEMO}")

# Identity (Mazi/Arlo) is stamped into who.c slots after compile. Do not
# pass FAMILY_DEVICE_* here — that forced a second full build per kit.
target_link_options(
    ${COMPONENT_LIB}
    INTERFACE
        "-Wl,-u,WHO_DEVICE_ID"
        "-Wl,-u,WHO_DEVICE_TOKEN"
        "-Wl,-u,WHO_DEVICE_NAME"
        "-Wl,-u,WHO_PEER_NAME"
)
