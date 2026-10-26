// bsp_ai_rgb.h
#ifndef BSP_AI_RGB_H
#define BSP_AI_RGB_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/** 板载 WS2812C RGB 灯数据脚（与 esp32s3_core 的 bspled 一致） */
#define BSP_AI_RGB_GPIO (45)

/**
 * 初始化 RGB 灯并从 SD 卡恢复上次保存的颜色。
 *
 * 配置文件不存在或 SD 卡未挂载时使用默认颜色，不会报错。
 *
 * @return esp_err_t LED 驱动初始化成功返回 ESP_OK。
 */
esp_err_t bsp_ai_rgb_init(void);

/**
 * 设置颜色与亮度并立即刷新到 LED。
 *
 * @param r 红色 0~255
 * @param g 绿色 0~255
 * @param b 蓝色 0~255
 * @param brightness 亮度百分比 0~100
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_rgb_set(uint8_t r, uint8_t g, uint8_t b, uint8_t brightness);

/**
 * 按当前颜色与亮度刷新 LED。
 */
esp_err_t bsp_ai_rgb_apply(void);

/**
 * 将当前颜色与亮度写入 BSP_AI_RGB_FILE。
 */
esp_err_t bsp_ai_rgb_save(void);

/**
 * 从 BSP_AI_RGB_FILE 读取颜色与亮度；文件不存在时保持当前值。
 */
esp_err_t bsp_ai_rgb_load(void);

uint8_t bsp_ai_rgb_red(void);
uint8_t bsp_ai_rgb_green(void);
uint8_t bsp_ai_rgb_blue(void);
uint8_t bsp_ai_rgb_brightness(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_RGB_H
