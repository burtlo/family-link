cmake_minimum_required(VERSION 3.16)
project(view_controller_host C CXX)
get_filename_component(REPO "${CMAKE_CURRENT_LIST_DIR}/../../.." ABSOLUTE)
set(LV_BUILD_CONF_PATH "${CMAKE_CURRENT_LIST_DIR}/lv_conf.h" CACHE PATH "" FORCE)
set(CONFIG_LV_BUILD_DEMOS OFF CACHE BOOL "" FORCE)
set(CONFIG_LV_BUILD_EXAMPLES OFF CACHE BOOL "" FORCE)
set(LV_BUILD_INSTALL OFF CACHE BOOL "" FORCE)
add_subdirectory("${REPO}/client/device/managed_components/lvgl__lvgl" lvgl)
add_executable(view_controller_test
    view_controller_test.cpp
    "${REPO}/client/device/main/product_view_controller.cpp"
    "${REPO}/client/device/main/product_split_pane_view_controller.cpp"
    "${REPO}/client/device/main/product_login_service.cpp"
    "${REPO}/client/device/main/product_login_view_controller.cpp"
    "${REPO}/client/device/main/product_runtime.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/session.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/pin_session.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/scoped_session.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/credential_session.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/pollers.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/p1_wire.cpp"
    "${REPO}/client/shared/login_session/fl_product_login/workers.cpp"
    "${REPO}/client/device/main/product_transition_coordinator.cpp")
target_include_directories(view_controller_test PRIVATE "${REPO}/client/device/main")
target_include_directories(view_controller_test PRIVATE "${REPO}/client/shared/login_session/fl_product_login/include")
target_compile_features(view_controller_test PRIVATE cxx_std_14)
target_link_libraries(view_controller_test PRIVATE lvgl m)
