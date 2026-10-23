set(PROMOTED_ROOT "${CMAKE_CURRENT_LIST_DIR}/../promoted")

idf_component_register(
    SRCS
        "session.cpp"
        "storage.cpp"
        "media_file.cpp"
        "flsk2.cpp"
        "${PROMOTED_ROOT}/lab10_ui/lab10_ui.c"
        "${PROMOTED_ROOT}/lab10_capture/lab10_capture.c"
        "${PROMOTED_ROOT}/lab10_render/lab10_render.c"
    INCLUDE_DIRS
        "include"
        "${PROMOTED_ROOT}/lab10_ui/include"
        "${PROMOTED_ROOT}/lab10_capture/include"
        "${PROMOTED_ROOT}/lab10_render/include")
