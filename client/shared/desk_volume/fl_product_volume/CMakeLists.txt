idf_component_register(
    SRCS "stops.cpp" "storage.cpp" "session.cpp" "journey.cpp"
    INCLUDE_DIRS "include"
    REQUIRES fl_product_inbox fl_product_login)
