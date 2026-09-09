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
        "${CMAKE_CURRENT_LIST_DIR}/../common/who.c"
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

# Identity (Mazi/Arlo) is stamped into who.c slots after compile. Do not
# pass FAMILY_DEVICE_* here — that forced a second full build per kit.
