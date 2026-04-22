idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES spiffs doomgeneric esp_timer freertos esp_driver_i2c esp_psram)

spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)