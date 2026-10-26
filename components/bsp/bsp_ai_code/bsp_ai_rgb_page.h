// bsp_ai_rgb_page.h
#ifndef BSP_AI_RGB_PAGE_H
#define BSP_AI_RGB_PAGE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * 创建 RGB 灯调色页面（首次调用时创建，之后复用）。
 *
 * 页面提供 R/G/B 与亮度滑条、实时预览和保存按钮；
 * 返回主页面时自动保存到 /sdcard/rgb led/rgb.txt。
 *
 * @return lv_obj_t* 页面 screen，失败返回 NULL。
 */
lv_obj_t *bsp_ai_rgb_page_create(void);

/**
 * 用当前 RGB 模块的值同步滑条、标签和预览。
 */
void bsp_ai_rgb_page_sync(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_RGB_PAGE_H
