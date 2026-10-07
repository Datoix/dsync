idf_component_register(
    SRCS "src/audio_out.cpp"
    INCLUDE_DIRS "include"
    REQUIRES board idf_handles esp_driver_i2s freertos esp_ringbuf
)
