if(NOT DEFINED FAMILY_DEMO OR FAMILY_DEMO STREQUAL "")
    set(FAMILY_DEMO "h02_display_count")
endif()

set(DEMO_SRC "${CMAKE_CURRENT_LIST_DIR}/../demos/${FAMILY_DEMO}.c")
set(X02_V1_DEMOS x02_product_shell x02_opus_size_probe x02_opus_dep_only)
if(FAMILY_DEMO IN_LIST X02_V1_DEMOS)
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

set(OPUS_PROBE_SRCS "")
if(FAMILY_DEMO STREQUAL "x02_opus_size_probe")
    list(APPEND OPUS_PROBE_SRCS "${CMAKE_CURRENT_LIST_DIR}/../common/fl_opus_size_probe.c")
endif()
if(NOT EXISTS "${DEMO_SRC}")
    message(FATAL_ERROR "Unknown FAMILY_DEMO=${FAMILY_DEMO} (expected ${DEMO_SRC})")
endif()

set(SECRETS "${CMAKE_CURRENT_LIST_DIR}/../secrets.h")
if(NOT EXISTS "${SECRETS}")
    set(SECRETS "${CMAKE_CURRENT_LIST_DIR}/../secrets.example.h")
endif()

if(FAMILY_DEMO STREQUAL "h32_onchip_storage")
    # h32 is an isolated storage qualification image.  Keep product, Wi-Fi,
    # display, and identity code out of the binary and out of its measurements.
    set(COMMON_SRCS "")
else()
    set(COMMON_SRCS
        "${CMAKE_CURRENT_LIST_DIR}/../common/pass.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/board.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/wifi_sta.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/http_bearer.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/v1_sketch.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/who.c"
    )
endif()
set(MAIN_PRIVATE_REQUIRES "")
# ESP-IDF's early requirements pass does not inherit project cache variables
# such as FAMILY_DEMO. Declare the union here so H32 receives its SDK headers;
# unused static libraries do not add product code to the linked image.
if(CMAKE_BUILD_EARLY_EXPANSION)
    list(APPEND MAIN_PRIVATE_REQUIRES app_update spi_flash fatfs wear_levelling)
endif()
if(FAMILY_DEMO STREQUAL "h30_opus_local" OR FAMILY_DEMO STREQUAL "h31_opus_chunks"
        OR FAMILY_DEMO STREQUAL "x02_opus_size_probe" OR FAMILY_DEMO STREQUAL "x02_opus_dep_only")
    list(APPEND MAIN_PRIVATE_REQUIRES opus)
endif()
if(FAMILY_DEMO STREQUAL "h32_onchip_storage")
    set(MAIN_PRIV_REQUIRES
        esp_timer
        app_update
        nvs_flash
        mbedtls
        spi_flash
        fatfs
        wear_levelling
    )
else()
    set(MAIN_PRIV_REQUIRES
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
        ${MAIN_PRIVATE_REQUIRES}
    )
endif()

idf_component_register(
    SRCS
        "${DEMO_SRC}"
        ${V1_SRCS}
        ${OPUS_PROBE_SRCS}
        ${COMMON_SRCS}
    INCLUDE_DIRS
        "${CMAKE_CURRENT_LIST_DIR}/../common"
        "${CMAKE_CURRENT_LIST_DIR}/.."
        ${V1_INCLUDE}
    PRIV_REQUIRES
        ${MAIN_PRIV_REQUIRES}
)

# secrets.h is gitignored; demos that need SSID/PIN include it.
target_compile_definitions(${COMPONENT_LIB} PRIVATE FAMILY_DEMO_NAME="${FAMILY_DEMO}")
if(FAMILY_DEMO STREQUAL "h32_onchip_storage")
    # The controller uses a separate build directory for the preservation-only
    # sentinel image, preventing cache contamination of the experiment image.
    if(NOT DEFINED H32_SENTINEL_ONLY)
        set(H32_SENTINEL_ONLY 0)
    endif()
    string(LENGTH "${H32_RUN_EPOCH}" H32_RUN_EPOCH_LENGTH)
    if(NOT DEFINED H32_RUN_EPOCH OR NOT H32_RUN_EPOCH MATCHES "^[0-9a-f]+$" OR NOT H32_RUN_EPOCH_LENGTH EQUAL 32)
        message(FATAL_ERROR "H32_RUN_EPOCH must be exactly 32 lowercase hex characters")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        H32_SENTINEL_ONLY=${H32_SENTINEL_ONLY}
        H32_RUN_EPOCH="${H32_RUN_EPOCH}")
endif()

# Identity (Mazi/Arlo) is stamped into who.c slots after compile. Do not
# pass FAMILY_DEVICE_* here — that forced a second full build per kit.
set(WHO_LINK_OPTS
    "-Wl,-u,WHO_DEVICE_ID"
    "-Wl,-u,WHO_DEVICE_TOKEN"
    "-Wl,-u,WHO_DEVICE_NAME"
    "-Wl,-u,WHO_PEER_NAME"
)
if(FAMILY_DEMO STREQUAL "x02_opus_size_probe")
    list(APPEND WHO_LINK_OPTS "-Wl,-u,fl_opus_size_probe_force_link")
endif()
if(NOT FAMILY_DEMO STREQUAL "h32_onchip_storage")
    target_link_options(${COMPONENT_LIB} INTERFACE ${WHO_LINK_OPTS})
endif()
