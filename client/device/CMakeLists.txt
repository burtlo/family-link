cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/stage_shell/fl_shell_model"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/stage_shell/product_shell"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/login_session/fl_product_login"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/inbox_carousel/fl_product_inbox"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/record_capture/fl_product_record"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/message_delivery/fl_product_send"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/desk_volume/fl_product_volume")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(family_link_bootstrap VERSION 0.1.0)
