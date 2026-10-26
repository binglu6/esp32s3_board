// bsp_lvgl_display.h
#ifndef BSP_LVGL_DISPLAY_H  // 防止头文件重复包含
#define BSP_LVGL_DISPLAY_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "logprint.h"
#include "bsp_ili9341.h"
#include "esp_lcd_touch_ft5x06.h"
#include "esp_lvgl_port.h"
#include "lv_demos.h"


#define EXAMPLE_TOUCH_I2C_CLK_HZ    (400000)
#define LCD_DRAW_BUFF_DOUBLE (0)
#define LCD_DRAW_BUFF_HEIGHT 10


void bsp_lvgl_start(void);
esp_err_t bsp_lvgl_display_init(void);

#ifdef __cplusplus
}
#endif

#endif // bsp_lvgl_display.h