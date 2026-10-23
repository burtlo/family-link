idf_component_register(
    SRCS "classify.cpp" "journey.cpp" "outbox.cpp" "p1_wire.cpp" "picker.cpp" "sha256.cpp"
         "send_stage.cpp" "workers.cpp"
    INCLUDE_DIRS "include"
    REQUIRES fl_product_login)
