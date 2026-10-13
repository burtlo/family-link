cmake_minimum_required(VERSION 3.16)
project(lab02_c0 C)
get_filename_component(LAB "${CMAKE_CURRENT_LIST_DIR}/../.." ABSOLUTE)
set(LV_CONF_SKIP ON CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_USE_THORVG OFF CACHE BOOL "" FORCE)
add_subdirectory("${LAB}/managed_components/lvgl__lvgl" lvgl)
set(CORE "${LAB}/components/lab02")
add_executable(lab02_c0 test_carousel_baseline.c
  "${CORE}/carousel_controller.c" "${CORE}/card_pool.c"
  "${CORE}/card_view.c" "${CORE}/card_view_layout.c" "${CORE}/snap.c"
  "${CORE}/snap_math.c" "${CORE}/lab02_session.c" "${CORE}/inbox_model.c"
  "${CORE}/inbox_page_metadata.c" "${CORE}/instrumentation.c"
  "${CJSON_DIR}/cJSON.c")
if(DEFINED LAB02_SLOW_BIND_MS)
  target_compile_definitions(lab02_c0 PRIVATE CONFIG_LAB02_SLOW_BIND_MS=${LAB02_SLOW_BIND_MS})
endif()
target_compile_features(lab02_c0 PRIVATE c_std_11)
target_compile_definitions(lab02_c0 PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(lab02_c0 PRIVATE -Wall -Wextra)
target_include_directories(lab02_c0 PRIVATE . "${LAB}/tests/host_stubs"
  "${CORE}/include" "${CJSON_DIR}")
target_link_libraries(lab02_c0 PRIVATE lvgl pthread m)

# Focused F008 gate uses the actual provider, with task/HTTP fault adapters.
get_target_property(F008_SOURCES lab02_c0 SOURCES)
list(REMOVE_ITEM F008_SOURCES test_carousel_baseline.c)
add_executable(lab02_initial_retry test_initial_retry_runtime.c ${F008_SOURCES}
  "${CORE}/page_provider_http.c")
target_compile_features(lab02_initial_retry PRIVATE c_std_11)
target_compile_definitions(lab02_initial_retry PRIVATE _POSIX_C_SOURCE=200809L LAB02_REAL_HTTP_PROVIDER)
target_compile_options(lab02_initial_retry PRIVATE -Wall -Wextra)
target_include_directories(lab02_initial_retry PRIVATE "${LAB}/tests/http_host_stubs" .
  "${LAB}/tests/host_stubs" "${CORE}/include" "${LAB}/components/lab02_platform/include" "${CJSON_DIR}")
target_link_libraries(lab02_initial_retry PRIVATE lvgl pthread m)
if(DEFINED LAB02_SLOW_BIND_MS)
  target_compile_definitions(lab02_initial_retry PRIVATE CONFIG_LAB02_SLOW_BIND_MS=${LAB02_SLOW_BIND_MS})
endif()

# F009 stable identity resolution, sharing actual production and LVGL sources.
get_target_property(IDENTITY_SOURCES lab02_c0 SOURCES)
list(REMOVE_ITEM IDENTITY_SOURCES test_carousel_baseline.c)
add_executable(lab02_identity test_identity_runtime.c ${IDENTITY_SOURCES})
target_compile_features(lab02_identity PRIVATE c_std_11)
target_compile_definitions(lab02_identity PRIVATE _POSIX_C_SOURCE=200809L)
target_compile_options(lab02_identity PRIVATE -Wall -Wextra)
target_include_directories(lab02_identity PRIVATE . "${LAB}/tests/host_stubs" "${CORE}/include" "${CJSON_DIR}")
target_link_libraries(lab02_identity PRIVATE lvgl pthread m)
if(DEFINED LAB02_SLOW_BIND_MS)
  target_compile_definitions(lab02_identity PRIVATE CONFIG_LAB02_SLOW_BIND_MS=${LAB02_SLOW_BIND_MS})
endif()
