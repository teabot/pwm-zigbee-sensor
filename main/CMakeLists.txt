idf_component_register(
    SRCS "main.c" "pwm_capture.c" "zigbee.c"
    INCLUDE_DIRS "."
    REQUIRES driver nvs_flash esp_timer freertos
)
