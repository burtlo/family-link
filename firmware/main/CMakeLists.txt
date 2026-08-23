if(NOT DEFINED FAMILY_DEMO OR FAMILY_DEMO STREQUAL "")
    set(FAMILY_DEMO "h02_display_count")
endif()

set(DEMO_SRC "${CMAKE_CURRENT_LIST_DIR}/../demos/${FAMILY_DEMO}.c")
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
        "${CMAKE_CURRENT_LIST_DIR}/../common/pass.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/board.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/wifi_sta.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/http_bearer.c"
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../common"
        "${CMAKE_CURRENT_LIST_DIR}/.."
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
if(FAMILY_SERVER_HOST)
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_SERVER_HOST="${FAMILY_SERVER_HOST}")
endif()
if(NOT FAMILY_SERVER_PORT STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_SERVER_PORT=${FAMILY_SERVER_PORT})
endif()
if(NOT FAMILY_SERVER_TLS STREQUAL "")
    target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_SERVER_TLS=${FAMILY_SERVER_TLS})
endif()
