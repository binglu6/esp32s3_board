// bsp_gc0308.h
#ifndef BSP_GC0308_H  // 防止头文件重复包含
#define BSP_GC0308_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "logprint.h"
#include "i2c_pca9557.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_camera.h"
#include "bsp_ili9341.h"

#define CAMERA_PIN_PWDN -1
#define CAMERA_PIN_RESET -1
#define CAMERA_PIN_XCLK 5
#define CAMERA_PIN_SIOD 1
#define CAMERA_PIN_SIOC 2

#define CAMERA_PIN_D7 9
#define CAMERA_PIN_D6 4
#define CAMERA_PIN_D5 6
#define CAMERA_PIN_D4 15
#define CAMERA_PIN_D3 10
#define CAMERA_PIN_D2 12
#define CAMERA_PIN_D1 11
#define CAMERA_PIN_D0 16
#define CAMERA_PIN_VSYNC 3
#define CAMERA_PIN_HREF 46
#define CAMERA_PIN_PCLK 7

#define XCLK_FREQ_HZ 24000000

void bsp_camera_init(void);
void app_camera_lcd(void);

#ifdef __cplusplus
}
#endif

#endif // bsp_gc0308.h