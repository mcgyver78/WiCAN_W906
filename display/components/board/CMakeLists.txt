# The hardware of the Elecrow CrowPanel 2.1 rotary display. esp_lcd is public: board.h hands out a panel handle.
# The ST7701 driver and its 3-wire SPI are managed components (idf_component.yml); the component manager adds
# them to the requirements by itself.
idf_component_register(SRCS "board.c"
                       INCLUDE_DIRS "."
                       REQUIRES esp_lcd
                       PRIV_REQUIRES esp_driver_i2c esp_driver_ledc esp_driver_pcnt esp_driver_tsens)
