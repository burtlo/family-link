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

set(H38_SRCS "")
if(FAMILY_DEMO STREQUAL "h38_sdmmc_filesystem")
    list(APPEND H38_SRCS
        "${CMAKE_CURRENT_LIST_DIR}/../common/h38_disk_guard.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/h38_fatfs_adapter.c"
        "${CMAKE_CURRENT_LIST_DIR}/../common/h38_filesystem_io.c")
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

if(FAMILY_DEMO STREQUAL "h32_onchip_storage" OR FAMILY_DEMO STREQUAL "h35_sdmmc_discovery" OR FAMILY_DEMO STREQUAL "h37_sdmmc_classification" OR FAMILY_DEMO STREQUAL "h38_sdmmc_filesystem")
    # H32/H35/H37/H38 are isolated storage qualification images. Keep product, Wi-Fi,
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
    list(APPEND MAIN_PRIVATE_REQUIRES app_update spi_flash fatfs wear_levelling sdmmc esp_driver_sdmmc esp_driver_gpio esp_driver_usb_serial_jtag esp_timer mbedtls esp_hw_support)
endif()
if(FAMILY_DEMO STREQUAL "h30_opus_local" OR FAMILY_DEMO STREQUAL "h31_opus_chunks"
        OR FAMILY_DEMO STREQUAL "x02_opus_size_probe" OR FAMILY_DEMO STREQUAL "x02_opus_dep_only")
    list(APPEND MAIN_PRIVATE_REQUIRES opus)
endif()
if(FAMILY_DEMO STREQUAL "h35_sdmmc_discovery" OR FAMILY_DEMO STREQUAL "h37_sdmmc_classification")
    set(MAIN_PRIV_REQUIRES esp_timer mbedtls sdmmc esp_driver_sdmmc esp_driver_gpio esp_driver_usb_serial_jtag)
elseif(FAMILY_DEMO STREQUAL "h38_sdmmc_filesystem")
    set(MAIN_PRIV_REQUIRES esp_timer mbedtls fatfs sdmmc esp_driver_sdmmc esp_driver_gpio esp_driver_usb_serial_jtag esp_hw_support)
elseif(FAMILY_DEMO STREQUAL "h32_onchip_storage")
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
        ${H38_SRCS}
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
    if(NOT DEFINED H32_PROFILE OR NOT H32_PROFILE MATCHES "^(baseline|no_yield|bounded_yield)$")
        message(FATAL_ERROR "H32_PROFILE must be baseline, no_yield or bounded_yield")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        H32_PROFILE="${H32_PROFILE}"
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
if(FAMILY_DEMO STREQUAL "h35_sdmmc_discovery")
    string(LENGTH "${H35_RUN_EPOCH}" H35_RUN_EPOCH_LENGTH)
    if(NOT DEFINED H35_RUN_EPOCH OR NOT H35_RUN_EPOCH MATCHES "^[0-9a-f]+$" OR NOT H35_RUN_EPOCH_LENGTH EQUAL 32)
        message(FATAL_ERROR "H35_RUN_EPOCH must be exactly 32 lowercase hex characters")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE H35_RUN_EPOCH="${H35_RUN_EPOCH}")
endif()
if(FAMILY_DEMO STREQUAL "h37_sdmmc_classification")
    foreach(_epoch H37_RUN_EPOCH H35_RUN_EPOCH)
        string(LENGTH "${${_epoch}}" _epoch_length)
        if(NOT DEFINED ${_epoch} OR NOT "${${_epoch}}" MATCHES "^[0-9a-f]+$" OR NOT _epoch_length EQUAL 32)
            message(FATAL_ERROR "${_epoch} must be exactly 32 lowercase hex characters")
        endif()
    endforeach()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        H37_RUN_EPOCH="${H37_RUN_EPOCH}"
        H35_RUN_EPOCH="${H35_RUN_EPOCH}")
endif()
if(NOT FAMILY_DEMO STREQUAL "h32_onchip_storage" AND NOT FAMILY_DEMO STREQUAL "h35_sdmmc_discovery" AND NOT FAMILY_DEMO STREQUAL "h37_sdmmc_classification" AND NOT FAMILY_DEMO STREQUAL "h38_sdmmc_filesystem")
    target_link_options(${COMPONENT_LIB} INTERFACE ${WHO_LINK_OPTS})
endif()

if(FAMILY_DEMO STREQUAL "h38_sdmmc_filesystem")
    foreach(_epoch H38_RUN_EPOCH H35_REFERENCE_EPOCH)
        string(LENGTH "${${_epoch}}" _epoch_length)
        if(NOT DEFINED ${_epoch} OR NOT "${${_epoch}}" MATCHES "^[0-9a-f]+$" OR NOT _epoch_length EQUAL 32)
            message(FATAL_ERROR "${_epoch} must be exactly 32 lowercase hex characters")
        endif()
    endforeach()
    if(NOT DEFINED H38_INTENT_HEADER OR NOT EXISTS "${H38_INTENT_HEADER}")
        message(FATAL_ERROR "H38_INTENT_HEADER must name the private generated intent header")
    endif()
    target_compile_definitions(${COMPONENT_LIB} PRIVATE
        H38_RUN_EPOCH="${H38_RUN_EPOCH}"
        H35_REFERENCE_EPOCH="${H35_REFERENCE_EPOCH}"
        H38_INTENT_HEADER=\"${H38_INTENT_HEADER}\")
endif()
