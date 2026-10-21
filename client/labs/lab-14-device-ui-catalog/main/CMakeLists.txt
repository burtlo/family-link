get_filename_component(REPO_ROOT "${CMAKE_CURRENT_LIST_DIR}/../../../.." ABSOLUTE)
set(CATALOG_JSON "${CMAKE_CURRENT_LIST_DIR}/../generated/catalog.json")
set(CATALOG_SRC "${REPO_ROOT}/docs/product/device-ui/catalog")
set(CATALOG_PACKER "${REPO_ROOT}/scripts/device_ui_catalog.py")

# ESP-IDF exports VIRTUAL_ENV to its own venv, which does not have PyYAML or
# jsonschema. Prefer the repo host venv; otherwise keep the committed pack.
set(CATALOG_PYTHON "")
foreach(_candidate
        "${REPO_ROOT}/.venv/bin/python"
        "${REPO_ROOT}/.venv/bin/python3"
        "${REPO_ROOT}/.venv/Scripts/python.exe")
    if(EXISTS "${_candidate}")
        set(CATALOG_PYTHON "${_candidate}")
        break()
    endif()
endforeach()

if(CATALOG_PYTHON)
    execute_process(
        COMMAND "${CATALOG_PYTHON}" "${CATALOG_PACKER}" pack
            --catalog "${CATALOG_SRC}"
            --out "${CATALOG_JSON}"
        RESULT_VARIABLE CATALOG_PACK_RESULT
        OUTPUT_VARIABLE CATALOG_PACK_OUTPUT
        ERROR_VARIABLE CATALOG_PACK_ERROR
    )
    if(NOT CATALOG_PACK_RESULT EQUAL 0)
        if(EXISTS "${CATALOG_JSON}")
            message(WARNING "device UI catalog pack failed; using existing ${CATALOG_JSON}: ${CATALOG_PACK_ERROR}${CATALOG_PACK_OUTPUT}")
        else()
            message(FATAL_ERROR "device UI catalog pack failed: ${CATALOG_PACK_ERROR}${CATALOG_PACK_OUTPUT}")
        endif()
    endif()
elseif(NOT EXISTS "${CATALOG_JSON}")
    message(FATAL_ERROR "packed catalog missing (${CATALOG_JSON}) and repo .venv python was not found")
endif()

idf_component_register(
    SRCS "app_main.c" "lab14_catalog.c" "lab14_ui.c"
    INCLUDE_DIRS "."
    EMBED_TXTFILES "${CATALOG_JSON}"
    REQUIRES esp-box-3 esp_lvgl_port json freertos esp_driver_gpio
)
