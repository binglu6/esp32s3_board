// bsp_ai_music.h
#ifndef BSP_AI_MUSIC_H
#define BSP_AI_MUSIC_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * 创建音乐播放页面（首次调用时创建，之后复用）。
 *
 * 页面包含返回、列表、上一首、播放/暂停、下一首与音量控制。
 * 必须在 LVGL 任务上下文或已持有 lvgl_port_lock() 时调用。
 *
 * @return lv_obj_t* 页面 screen，失败返回 NULL。
 */
lv_obj_t *bsp_ai_music_page_create(void);

/**
 * 刷新歌曲名、播放状态、播放/暂停图标与音量显示。
 * 必须在 LVGL 任务上下文或已持有 lvgl_port_lock() 时调用。
 */
void bsp_ai_music_page_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_MUSIC_H
