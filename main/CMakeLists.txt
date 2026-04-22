idf_component_register(SRCS "main.c"
                    INCLUDE_DIRS "."
                    REQUIRES spiffs doomgeneric esp_timer freertos)

spiffs_create_partition_image(storage ../data FLASH_IN_PROJECT)