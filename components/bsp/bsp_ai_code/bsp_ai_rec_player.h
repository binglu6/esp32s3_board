// bsp_ai_rec_player.h
#ifndef BSP_AI_REC_PLAYER_H
#define BSP_AI_REC_PLAYER_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * 创建录音播放页面（首次调用时创建，之后复用）。
 *
 * 与音乐播放页面布局一致，但只播放 /sdcard/录音文件 下的 wav，
 * 返回按钮回到录音文件夹列表。
 *
 * @return lv_obj_t* 页面 screen，失败返回 NULL。
 */
lv_obj_t *bsp_ai_rec_player_create(void);

/**
 * 刷新录音名、播放状态、播放/暂停图标与音量显示。
 */
void bsp_ai_rec_player_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_REC_PLAYER_H
