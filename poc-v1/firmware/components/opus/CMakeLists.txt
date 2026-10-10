# Pulls in 78/esp-opus (libopus) from the ESP Component Registry.
set(FL_OPUS_SRC "${CMAKE_CURRENT_LIST_DIR}/../../common/fl_opus.c")
idf_component_register(
    SRCS "component_mark.c" "${FL_OPUS_SRC}"
    INCLUDE_DIRS "." "${CMAKE_CURRENT_LIST_DIR}/../../common"
    REQUIRES 78__esp-opus
)
