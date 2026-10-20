idf_component_register(SRCS "model.cpp" INCLUDE_DIRS "include")

# Keep the verified MODEL source unchanged while allowing its native-tested
# single-line guards under ESP-IDF's global -Werror policy.
target_compile_options(${COMPONENT_LIB} PRIVATE -Wno-error=misleading-indentation)
