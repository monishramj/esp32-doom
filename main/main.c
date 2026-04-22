#include <stdio.h>
#include <stdint.h>
#include <string.h>
#include <stdlib.h>

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
    i2c_master_transmit(oled_dev_handle, data, sizeof(data), 100);
}

void oled_data(uint8_t *buffer, size_t len) {
    static uint8_t transmission[1024 + 1];
    transmission[0] = 0x40; 
    memcpy(&transmission[1], buffer, len);
    i2c_master_transmit(oled_dev_handle, transmission, len + 1, 100);
}

void ssd1306_init() {
    vTaskDelay(pdMS_TO_TICKS(100));
    oled_cmd(0xAE); 
    oled_cmd(0xD5); oled_cmd(0x80); 
    oled_cmd(0xA8); oled_cmd(0x3F); 
    oled_cmd(0xD3); oled_cmd(0x00); 
    oled_cmd(0x40); 
    oled_cmd(0x20); oled_cmd(0x00); 
    oled_cmd(0x21); oled_cmd(0); oled_cmd(127);
    oled_cmd(0x22); oled_cmd(0); oled_cmd(7);
    oled_cmd(0xA1); 
    oled_cmd(0xC8); 
    oled_cmd(0xDA); oled_cmd(0x12); 
    oled_cmd(0x81); oled_cmd(0xFF); 
    oled_cmd(0xD9); oled_cmd(0xF1); 
    oled_cmd(0xDB); oled_cmd(0x40); 
    oled_cmd(0xA4); 
    oled_cmd(0xA6); 
    oled_cmd(0x8D); oled_cmd(0x14); 
    oled_cmd(0xAF); 
}

void mount_wad() {
    esp_vfs_spiffs_conf_t conf = {
        .base_path = "/spiffs", .partition_label = "storage",
        .max_files = 5, .format_if_mount_failed = true
    };
    esp_vfs_spiffs_register(&conf);
}

void oled_reset_pointers() {
    uint8_t reset_seq[] = {0x00, 0x21, 0, 127, 0x22, 0, 7};
    i2c_master_transmit(oled_dev_handle, reset_seq, sizeof(reset_seq), 100);
}

void DG_Init() {}

// FIX: added 'static' to keep the linker happy
static inline uint8_t get_lum_fast(uint32_t* buf, int x, int y) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return 0;
    uint32_t c = buf[y * 320 + x];
    // fast luminance approx: (2R + 5G + B) / 8
    return (uint8_t)((((c >> 16) & 0xFF) * 2 + ((c >> 8) & 0xFF) * 5 + (c & 0xFF)) >> 3);
}

void DG_DrawFrame() {
    oled_reset_pointers();
    uint8_t oled_buf[1024] = {0};
    uint32_t* pixel_data = (uint32_t*) DG_ScreenBuffer;

    static const uint8_t bayer_4x4[4][4] = {
        { 15, 135,  45, 165 }, { 195,  75, 225, 105 },
        { 60, 180,  30, 150 }, { 240, 120, 210,  90 }
    };

    // based on ur log, the engine is rendering 640x400
    // so every 1 pixel on oled = a 5x6.25 block in the buffer
    for (int y = 0; y < 64; y++) {
        int dy = y * 6; // 400 / 64 approx 6.25
        uint32_t* row_ptr = &pixel_data[dy * 640];

        for (int x = 0; x < 128; x++) {
            int dx = x * 5; // 640 / 128 = 5

            uint32_t color = row_ptr[dx];
            
            // fast lum
            uint16_t lum = (((color >> 16) & 0xFF) * 2 + ((color >> 8) & 0xFF) * 5 + (color & 0xFF)) >> 3;

            // contrast pop: crush blacks, stretch whites
            if (lum < 60) {
                lum = 0;
            } else {
                lum = ((lum - 60) * 255) / 195;
                lum = (lum * 15) / 10; // 1.5x boost
                if (lum > 255) lum = 255;
            }

            if (lum > bayer_4x4[y & 3][x & 3]) {
                oled_buf[x + (y >> 3) * 128] |= (1 << (y & 7));
            }
        }
    }
    oled_data(oled_buf, 1024);
}

uint32_t DG_GetTicksMs() { return (uint32_t)(esp_timer_get_time() / 1000); }
void DG_SleepMs(uint32_t ms) { vTaskDelay(pdMS_TO_TICKS(ms)); }
int DG_GetKey(int* pressed, unsigned char* key) { return 0; }
void DG_SetWindowTitle(const char * title) { printf("doom window: %s\n", title); }
//tree = true! - russ 2026
void app_main(void) {
    mount_wad();
    init_i2c();
    ssd1306_init();
    printf("hardware is set up. booting engine...\n");

    char* args[] = { "doom", "-iwad", "/spiffs/doom1.wad" };
    doomgeneric_Create(3, args);

    while (1) { 
        static uint32_t last_frame_time = 0;
        static int frame_count = 0;

        doomgeneric_Tick();
        frame_count++;

        uint32_t now = DG_GetTicksMs();
        if (now - last_frame_time >= 1000) {
            printf("FPS: %d\n", frame_count);
            frame_count = 0;
            last_frame_time = now;
        }
    }
}