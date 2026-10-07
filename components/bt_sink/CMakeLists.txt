idf_component_register(
    SRCS "src/bt_sink.cpp"
    INCLUDE_DIRS "include"
    REQUIRES audio_out ui_leds idf_handles
    PRIV_REQUIRES bt nvs_flash freertos
)
