# Flappy Bird application. The baseline hardware-test demo sources stay in this
# directory for the shared host tests but are not built into this firmware.
idf_component_register(
    SRCS "main.c"
         "flappy_model.c"
         "flappy_sprite.c"
         "flappy_sfx.c"
         "flappy_ui.c"
         "flappy_services.c"
    INCLUDE_DIRS "."
    REQUIRES bsp esp_timer esp_hw_support nvs_flash
)
