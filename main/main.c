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

#include "driver/i2c_master.h"


#define I2C_PORT I2C_NUM_0
#define SDA_GPIO 7
#define SCL_GPIO 15
#define OLED_ADDRESS 0x3C
#define SCREEN_WIDTH 128
#define SCREEN_HEIGHT 64
#define SCL_SPEED_HZ 1000000


int drone = 0;
int net_client_connected = 0;
i2c_master_dev_handle_t oled_dev_handle;

void init_i2c() {

    const i2c_master_bus_config_t bus_config = {
        .i2c_port = I2C_NUM_0,
        .sda_io_num = SDA_GPIO,
        .scl_io_num = SCL_GPIO,
        .clk_source = I2C_CLK_SRC_DEFAULT,
        .glitch_ignore_cnt = 7,
    };

    i2c_master_bus_handle_t bus_handle; 
    ESP_ERROR_CHECK(i2c_new_master_bus(&bus_config, &bus_handle));

    i2c_device_config_t dev_config = {
        .dev_addr_length = I2C_ADDR_BIT_LEN_7,
        .device_address = OLED_ADDRESS,
        .scl_speed_hz = SCL_SPEED_HZ,
    };
    ESP_ERROR_CHECK(i2c_master_bus_add_device(bus_handle, &dev_config, &oled_dev_handle));
}

void oled_cmd(uint8_t cmd) {
    uint8_t data[2] = {0x00, cmd};

    ESP_ERROR_CHECK(i2c_master_transmit(oled_dev_handle, data, sizeof(data), 100));
}

void oled_data(uint8_t *buffer, size_t len) {

    static uint8_t transmission[1024 + 1];

    transmission[0] = 0x40; // data mode?
    memcpy(&transmission[1], buffer, len);

    i2c_master_transmit(oled_dev_handle, transmission, len + 1, 100);
}

void ssd1306_init() {

    oled_cmd(0xAE); // display off

    oled_cmd(0x8D); // turns on internal high-voltage for pixels
    oled_cmd(0x14); // turns on internal high-voltage for pixels

    oled_cmd(0x20); // horizontal mode 
    oled_cmd(0x00); // horizontal mode 
    
    oled_cmd(0x81); //contrast controll
    oled_cmd(0xFF); //contrast controll

    oled_cmd(0xD9); // sharper pixels
    oled_cmd(0xF1); // sharper pixels

    oled_cmd(0xAF); // display on

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


void DG_Init() {}

void DG_DrawFrame() {
    oled_cmd(0x21);
    oled_cmd(0);
    oled_cmd(127);

    oled_cmd(0x22);
    oled_cmd(0);
    oled_cmd(7);

    uint8_t oled_buf[1024] = {0};
    uint32_t* pixel_data = (uint32_t*) DG_ScreenBuffer;

    // 4x4 Bayer dither matrix (ong gemini is goated)
    static const uint8_t bayer_4x4[4][4] = {
        { 15, 135,  45, 165 },
        { 195,  75, 225, 105 },
        { 60, 180,  30, 150 },
        { 240, 120, 210,  90 }
    };

    for (int y = 0; y < SCREEN_HEIGHT; y++) {
        for (int x = 0; x < SCREEN_WIDTH; x++) {
            int dx = (x * 320) / SCREEN_WIDTH;
            int dy = (y * 200) / SCREEN_HEIGHT;

            uint32_t color = pixel_data[dy * 320 + dx];

            // extract r, g, b components
            uint8_t r = (color >> 16) & 0xFF;
            uint8_t g = (color >> 8) & 0xFF;
            uint8_t b = color & 0xFF;

            uint16_t brightness = (uint16_t)((r * 77) + (g * 150) + (b * 29)) >> 8;

            if (brightness > bayer_4x4[y % 4][x % 4]) {
                oled_buf[x + (y / 8) * 128] |= (1 << (y % 8));
            }
        }
    }

    oled_data(oled_buf, sizeof(oled_buf));
}

uint32_t DG_GetTicksMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void DG_SleepMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

int DG_GetKey(int* pressed, unsigned char* key) {return 0;}

void DG_SetWindowTitle(const char * title) {
    printf("doom window: %s\n", title);
}

void app_main(void) {

    mount_wad();
    init_i2c();
    ssd1306_init();

    printf("hardware is set up.\n");

    char* args[] = { "doom", "-iwad", "/spiffs/doom1.wad" };

    doomgeneric_Create(3, args);
    printf("booting up doom...\n");
    while (1) { 
        doomgeneric_Tick(); 
        vTaskDelay(pdMS_TO_TICKS(1)); 
    }
}
