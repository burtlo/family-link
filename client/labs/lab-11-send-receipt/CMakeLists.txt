cmake_minimum_required(VERSION 3.16)
set(EXTRA_COMPONENT_DIRS "components/lab11_capture" "components/lab11_send" "components/lab11_station")
include($ENV{IDF_PATH}/tools/cmake/project.cmake)
project(lab11_send_receipt)
