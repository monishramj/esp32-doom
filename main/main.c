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
#define I2C_BUS_SPEED 1000000

#define DOOM_RES_W 640
#define DOOM_RES_H 400
#define OLED_RES_W 128
#define OLED_RES_H 64
#define OLED_BUF_SIZE 1024

// how many pixels we skip (don't change this)
#define STRIDE_X (DOOM_RES_W / OLED_RES_W) 
#define STRIDE_Y (DOOM_RES_H / OLED_RES_H)

#define EDGE_THRESHOLD 35   // higher = cleaner world, lower = more outlines
#define LUM_THRESHOLD  40   // higher -> darker
#define CONTRAST_MUL   20
#define CONTRAST_DIV   10

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
        .scl_speed_hz = I2C_BUS_SPEED,
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
    const uint8_t init_seq[] = {
        0xAE, 0xD5, 0x80, 0xA8, 0x3F, 0xD3, 0x00, 0x40, 
        0x20, 0x00, 0x21, 0, 127, 0x22, 0, 7,
        0xA1, 0xC8, 0xDA, 0x12, 0x81, 0xFF, 0xD9, 0xF1, 
        0xDB, 0x40, 0xA4, 0xA6, 0x8D, 0x14, 0xAF
    };

    for(int i = 0; i < sizeof(init_seq); i++) oled_cmd(init_seq[i]);
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

static inline uint8_t get_lum_fast(uint32_t *buf, int x, int y) {
    if (x < 0 || x >= 320 || y < 0 || y >= 200) return 0;
    uint32_t c = buf[y * 320 + x];

    // fast luminance approx: (2R + 5G + B) / 8
    return (uint8_t)((((c >> 16) & 0xFF) * 2 + ((c >> 8) & 0xFF) * 5 + (c & 0xFF)) >> 3);
}

void DG_DrawFrame() {
    oled_reset_pointers();
    uint8_t oled_buf[OLED_BUF_SIZE] = {0};
    uint32_t *pixel_data = (uint32_t*) DG_ScreenBuffer;

    static const uint8_t bayer_4x4[4][4] = {
        { 15, 135, 45, 165 }, { 195, 75, 225, 105 },
        { 60, 180, 30, 150 }, { 240, 120, 210, 90 }
    };

    for (int y = 0; y < OLED_RES_H; y++) {
        uint32_t *row = &pixel_data[(y * STRIDE_Y) * DOOM_RES_W];
        uint32_t *next_row = &pixel_data[((y + 1) * STRIDE_Y) * DOOM_RES_W];

        for (int x = 0; x < OLED_RES_W; x++) {
            // 1. BOX SAMPLE (2x2 average) - kills the shimmering texture noise
            uint32_t c00 = row[x * STRIDE_X];
            uint32_t c01 = row[x * STRIDE_X + 1];
            uint32_t c10 = next_row[x * STRIDE_X];
            uint32_t c11 = next_row[x * STRIDE_X + 1];

            uint16_t r = (((c00>>16)&0xFF) + ((c01>>16)&0xFF) + ((c10>>16)&0xFF) + ((c11>>16)&0xFF)) >> 2;
            uint16_t g = (((c00>>8)&0xFF) + ((c01>>8)&0xFF) + ((c10>>8)&0xFF) + ((c11>>8)&0xFF)) >> 2;
            uint16_t b = ((c00&0xFF) + (c01&0xFF) + (c10&0xFF) + (c11&0xFF)) >> 2;

            uint16_t lum = (r * 2 + g * 5 + b) >> 3;

            uint16_t lum_r = (((c01>>16)&0xFF)*2 + ((c01>>8)&0xFF)*5 + (c01&0xFF)) >> 3;
            uint16_t lum_d = (((c10>>16)&0xFF)*2 + ((c10>>8)&0xFF)*5 + (c10&0xFF)) >> 3;
            uint16_t edge = abs(lum - lum_r) + abs(lum - lum_d);

            uint16_t final_lum = 0;
            if (lum > LUM_THRESHOLD) {
                final_lum = ((lum - LUM_THRESHOLD) * 255) / (255 - LUM_THRESHOLD);
                final_lum = (final_lum * CONTRAST_MUL) / CONTRAST_DIV;
                if (final_lum > 255) final_lum = 255;
            }

            if (edge > EDGE_THRESHOLD || final_lum > bayer_4x4[y & 3][x & 3]) {
                oled_buf[x + (y >> 3) * OLED_RES_W] |= (1 << (y & 7));
            }
        }
    }
    oled_data(oled_buf, OLED_BUF_SIZE);
}

uint32_t DG_GetTicksMs() {
    return (uint32_t)(esp_timer_get_time() / 1000);
}

void DG_SleepMs(uint32_t ms) {
    vTaskDelay(pdMS_TO_TICKS(ms));
}

int DG_GetKey(int *pressed, unsigned char *key) {
    return 0;
}

void DG_SetWindowTitle(const char *title) {
    printf("doom window: %s\n", title);
}

//tree = true! - russ 2026
void app_main(void) {
    mount_wad();
    init_i2c();
    ssd1306_init();
    printf("hardware is set up. booting engine...\n");

    char *args[] = { "doom", "-iwad", "/spiffs/doom1.wad" };
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