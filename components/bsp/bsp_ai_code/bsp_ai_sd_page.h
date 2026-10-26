// bsp_ai_sd_page.h
#ifndef BSP_AI_SD_PAGE_H
#define BSP_AI_SD_PAGE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * 创建 SD 卡浏览页面（首次调用时创建，之后复用）。
 *
 * 根目录显示 music / 录音文件 / rgb led 三个文件夹：
 *   - music：列出 mp3 / wav，点击进入音乐播放页；
 *   - 录音文件：列出 wav，点击进入录音播放页；
 *   - rgb led：列出 txt，点击查看颜色与亮度信息。
 *
 * 必须在 LVGL 任务上下文或已持有 lvgl_port_lock() 时调用。
 *
 * @return lv_obj_t* 页面 screen，失败返回 NULL。
 */
lv_obj_t *bsp_ai_sd_page_create(void);

/**
 * 打开指定目录（NULL 表示根目录）并刷新列表。
 *
 * @param dir 目录绝对路径，或 NULL
 */
void bsp_ai_sd_page_show_dir(const char *dir);

/**
 * 按当前视图重新扫描并重建列表。
 */
void bsp_ai_sd_page_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_SD_PAGE_H
