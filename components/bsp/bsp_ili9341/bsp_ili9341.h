// bsp_ili9341.h
#ifndef BSP_ILI9341_H  // 防止头文件重复包含
#define BSP_ILI9341_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "logprint.h"
#include "i2c_pca9557.h"
#include "driver/ledc.h"
#include "driver/spi_master.h"
#include "esp_lcd_panel_io.h"
#include "esp_lcd_panel_vendor.h"
#include "esp_lcd_panel_ops.h"
#include "esp_lcd_types.h"
#include "esp_lcd_ili9341.h"
#include <string.h>
#include "bsp_picture.h"

// #define BSP_picture_yinwu 1 //定义，是否开启鹦鹉图
#define BSP_LCD_BACKLIGHT     (GPIO_NUM_39)
#define LCD_LEDC_CH          (LEDC_CHANNEL_0) //0~7
#define BSP_LCD_H_RES              (320)
#define BSP_LCD_V_RES              (240)
#define BSP_LCD_SPI_MOSI      (GPIO_NUM_41)
#define BSP_LCD_SPI_MISO      (GPIO_NUM_38)
#define BSP_LCD_SPI_CLK       (GPIO_NUM_40)
#define BSP_LCD_SPI_CS        (GPIO_NUM_NC)
#define BSP_LCD_DC            (GPIO_NUM_42)
#define BSP_LCD_RST           (GPIO_NUM_NC)
#define BSP_LCD_PIXEL_CLOCK_HZ     (40 * 1000 * 1000)
#define LCD_CMD_BITS         (8)
#define LCD_PARAM_BITS       (8)
#define BSP_LCD_SPI_NUM            (SPI3_HOST)
/* LCD display color bits */
#define BSP_LCD_BITS_PER_PIXEL      (16)

extern esp_lcd_panel_handle_t lcd_panel;
extern esp_lcd_panel_io_handle_t lcd_io;
esp_err_t bsp_lcd_init(void);

#ifdef __cplusplus
}
#endif

#endif // bsp_ili9341.h