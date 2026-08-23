# ESP-IDF project: one BOX-3 demo per build (FAMILY_DEMO selects the .c file).
# Demos do not call each other. common/ is UART pass-line + optional BSP helpers.
cmake_minimum_required(VERSION 3.16)

set(FAMILY_DEMO "h02_display_count" CACHE STRING "Demo source stem under demos/")
set(FAMILY_SERVER_HOST "" CACHE STRING "Override DEMO_SERVER_HOST at compile time")
set(FAMILY_SERVER_PORT "" CACHE STRING "Override DEMO_SERVER_PORT at compile time")
set(FAMILY_SERVER_TLS "" CACHE STRING "Override DEMO_SERVER_TLS (0/1) at compile time")

include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(family_link_demo)
