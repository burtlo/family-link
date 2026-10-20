# Peak C is part of the lifecycle project tree, but it is not part of the
# lifecycle link. ESP-IDF discovers this component either way. When
# CONFIG_LAB05_PEAK is unset (the default), register no SRCS so the build
# does not compile or link lab05_peak_model.c or the device harness. A
# Kconfig `depends on` does not exclude a .c file that CMake has already listed.
# Requirements are recorded by ESP-IDF's early expansion, which can take the
# branch where CONFIG_LAB05_PEAK is still unset. List the same private
# requirements on both branches so the device harness can include the BSP.
# The else branch still has no C sources, so the lifecycle link does not
# compile the peak model or the harness.
set(LAB05_PEAK_PRIV_REQS espressif__esp-box-3 esp_timer heap freertos lvgl__lvgl)
if(CONFIG_LAB05_PEAK)
    idf_component_register(
        SRCS "lab05_peak_model.c" "lab05_peak_harness.c"
        INCLUDE_DIRS "include"
        PRIV_REQUIRES ${LAB05_PEAK_PRIV_REQS}
    )
else()
    idf_component_register(
        INCLUDE_DIRS "include"
        PRIV_REQUIRES ${LAB05_PEAK_PRIV_REQS}
    )
endif()
