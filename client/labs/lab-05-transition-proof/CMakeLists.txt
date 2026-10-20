cmake_minimum_required(VERSION 3.16)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lab05_transition_proof VERSION 0.1.0)

# ESP-IDF v5.4.2's xtensa-esp-elf 14.2.0 can segfault in the register
# allocator when esp_lcd's RGB panel source is compiled at -Og. Appending
# -O1 makes it the last optimization flag for that component. Lab sources
# keep the default optimization. This is not a memory-budget or frame-budget
# change.
idf_component_get_property(esp_lcd_lib esp_lcd COMPONENT_LIB)
target_compile_options(${esp_lcd_lib} PRIVATE "-O1")
message(STATUS "lab05 esp_lcd RGB compile workaround: -O1")
