idf_component_register(
    SRCS "src/ui_leds.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board idf_handles esp_driver_gpio esp_timer
)
