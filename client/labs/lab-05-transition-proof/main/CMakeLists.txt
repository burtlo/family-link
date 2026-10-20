if(CONFIG_LAB05_PEAK)
    idf_component_register(
        SRCS "app_main.c"
        REQUIRES lab05_peak
    )
else()
    idf_component_register(SRCS "app_main.c")
endif()
