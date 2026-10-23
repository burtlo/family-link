idf_component_register(
    SRCS "journey.cpp"
    INCLUDE_DIRS "include"
    REQUIRES fl_product_login fl_product_inbox fl_product_volume fl_product_send fl_shell_model)
