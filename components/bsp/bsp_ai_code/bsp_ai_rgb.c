// bsp_ai_rgb.c — WS2812C RGB 灯驱动与配置持久化
#include "bsp_ai_rgb.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_ai_sdcard.h"
#include "driver/spi_master.h"
#include "esp_check.h"
#include "esp_log.h"
#include "led_strip.h"

static const char *TAG = "bsp_ai_rgb";

#define BSP_AI_RGB_LED_NUM  (1)

static led_strip_handle_t s_strip = NULL;
static uint8_t s_red = 0;
static uint8_t s_green = 120;
static uint8_t s_blue = 255;
static uint8_t s_brightness = 70;

static uint8_t scale_color(uint8_t value)
{
    return (uint8_t)((uint32_t)value * s_brightness / 100U);
}

esp_err_t bsp_ai_rgb_apply(void)
{
    if (s_strip == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    ESP_RETURN_ON_ERROR(led_strip_set_pixel(s_strip, 0, scale_color(s_red),
                                            scale_color(s_green), scale_color(s_blue)),
                        TAG, "set pixel failed");
    return led_strip_refresh(s_strip);
}

esp_err_t bsp_ai_rgb_set(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness)
{
    s_red = r;
    s_green = g;
    s_blue = b;
    s_brightness = (brightness > 100) ? 100 : brightness;
    return bsp_ai_rgb_apply();
}

esp_err_t bsp_ai_rgb_save(void)
{
    if (!bsp_ai_sd_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)bsp_ai_sd_prepare_dirs();
    FILE *fp = fopen(BSP_AI_RGB_FILE, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open %s for write failed", BSP_AI_RGB_FILE);
        return ESP_FAIL;
    }
    fprintf(fp, "R=%u\nG=%u\nB=%u\nBRIGHTNESS=%u\n", s_red, s_green, s_blue, s_brightness);
    fclose(fp);
    ESP_LOGI(TAG, "saved R=%u G=%u B=%u brightness=%u", s_red, s_green, s_blue, s_brightness);
    return ESP_OK;
}

esp_err_t bsp_ai_rgb_load(void)
{
    FILE *fp = fopen(BSP_AI_RGB_FILE, "r");
    if (fp == NULL) {
        ESP_LOGI(TAG, "no saved rgb config, use default");
        return ESP_ERR_NOT_FOUND;
    }

    char line[64];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        int value = atoi(eq + 1);
        if (strncmp(line, "BRIGHTNESS", 10) == 0) {
            s_brightness = (value > 100) ? 100 : (uint8_t)value;
        } else if (line[0] == 'R') {
            s_red = (uint8_t)value;
        } else if (line[0] == 'G') {
            s_green = (uint8_t)value;
        } else if (line[0] == 'B') {
            s_blue = (uint8_t)value;
        }
    }
    fclose(fp);
    ESP_LOGI(TAG, "loaded R=%u G=%u B=%u brightness=%u", s_red, s_green, s_blue, s_brightness);
    return ESP_OK;
}

esp_err_t bsp_ai_rgb_init(void)
{
    led_strip_config_t strip_config = {
        .strip_gpio_num = BSP_AI_RGB_GPIO,
        .max_leds = BSP_AI_RGB_LED_NUM,
        .led_model = LED_MODEL_WS2812,
        .color_component_format = LED_STRIP_COLOR_COMPONENT_FMT_GRB,
        .flags.invert_out = false,
    };
    led_strip_spi_config_t spi_config = {
        .clk_src = SPI_CLK_SRC_DEFAULT,
        .spi_bus = SPI2_HOST,
        .flags.with_dma = true,
    };

    ESP_RETURN_ON_ERROR(led_strip_new_spi_device(&strip_config, &spi_config, &s_strip),
                        TAG, "create led strip failed");
    (void)bsp_ai_rgb_load();
    return bsp_ai_rgb_apply();
}

uint8_t bsp_ai_rgb_red(void)
{
    return s_red;
}

uint8_t bsp_ai_rgb_green(void)
{
    return s_green;
}

uint8_t bsp_ai_rgb_blue(void)
{
    return s_blue;
}

uint8_t bsp_ai_rgb_brightness(void)
{
    return s_brightness;
}
