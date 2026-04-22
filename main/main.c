#include <stdio.h>
#include <stdint.h>

#include "doomgeneric.h"

#include "freertos/FreeRTOS.h" 
#include "freertos/task.h"

#include "esp_log.h"  
#include "esp_timer.h"
#include "esp_task_wdt.h"
#include "esp_spiffs.h"
#include "esp_system.h"


// global vars for network/logic
int drone = 0;
int net_client_connected = 0;

void DG_Init() {}

void DG_DrawFrame() {}

uint32_t DG_GetTicksMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void DG_SleepMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

int DG_GetKey(int* pressed, unsigned char* key) {return 0;}

// window title (js print it for now)
void DG_SetWindowTitle(const char * title) {
    printf("doom window: %s\n", title);
}


void mount_wad() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs",
        .partition_label = "storage",
        .max_files = 5,
        .format_if_mount_failed = true
    };
    esp_vfs_spiffs_register(&conf);
}

void app_main(void) {

    mount_wad();
    char* args[] = { "doom", "-iwad", "/spiffs/doom1.wad" };

    doomgeneric_Create(3, args);
    printf("booting up doom...\n");
    while (1) { 
        doomgeneric_Tick(); 

        vTaskDelay(pdMS_TO_TICKS(1));
    }

}
