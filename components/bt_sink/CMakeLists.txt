idf_component_register(
    SRCS
        "src/bt_sink.cpp"
        "src/bt_controller.cpp"
        "src/bt_a2dp.cpp"
    INCLUDE_DIRS "include"
    REQUIRES audio_out ui_leds idf_handles
    PRIV_REQUIRES bt nvs_flash freertos
)
