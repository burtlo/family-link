# ESP-IDF project: one BOX-3 demo per build (FAMILY_DEMO selects the .c file).
# Demos do not call each other. common/ is UART pass-line + optional BSP helpers.
cmake_minimum_required(VERSION 3.16)

set(FAMILY_DEMO "h02_display_count" CACHE STRING "Demo source stem under demos/")
set(FAMILY_PARTITION_PROFILE "" CACHE STRING "Partition profile overlay (e.g. x02_2125)")

set(SDKCONFIG_DEFAULTS "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults")
if(FAMILY_DEMO STREQUAL "x02_product_shell"
        OR FAMILY_DEMO STREQUAL "x02_opus_size_probe"
        OR FAMILY_DEMO STREQUAL "x02_opus_dep_only")
    set(SDKCONFIG_DEFAULTS
        "${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults;${CMAKE_CURRENT_LIST_DIR}/sdkconfig.defaults.v1")
    if(FAMILY_PARTITION_PROFILE STREQUAL "x02_2125")
        set(SDKCONFIG_DEFAULTS
            "${SDKCONFIG_DEFAULTS};${CMAKE_CURRENT_LIST_DIR}/sdkconfig.x02_2125.defaults")
    endif()
    # Do not reuse firmware/sdkconfig (island/menuconfig); keep x02 Kconfig in this build dir.
    set(SDKCONFIG "${CMAKE_BINARY_DIR}/sdkconfig" CACHE FILEPATH "sdkconfig")
endif()

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(family_link_demo)
