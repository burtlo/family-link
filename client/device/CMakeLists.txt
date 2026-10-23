cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS
    "${CMAKE_CURRENT_LIST_DIR}/../shared/stage_shell/fl_shell_model"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/stage_shell/product_shell"
    "${CMAKE_CURRENT_LIST_DIR}/../shared/login_session/fl_product_login")
set(COMPONENTS main)
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(family_link_bootstrap VERSION 0.1.0)
