// bsp_ai_ui.h
#ifndef BSP_AI_UI_H
#define BSP_AI_UI_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

/**
 * 页面切换接口。
 *
 * 这些函数只做 lv_screen_load()，必须在 LVGL 任务上下文或已持有
 * lvgl_port_lock() 的情况下调用（LVGL 事件回调本身就在 LVGL 任务内）。
 */
void bsp_ai_show_main(void);
void bsp_ai_show_camera(void);
void bsp_ai_show_music(void);
void bsp_ai_show_rec(void);
void bsp_ai_show_rec_player(void);
void bsp_ai_show_rgb(void);
void bsp_ai_show_sd(void);
void bsp_ai_show_wifi(void);
void bsp_ai_show_ai(void);

/**
 * 打开 SD 卡页面并进入指定目录。
 *
 * @param dir 目录绝对路径；NULL 表示 SD 卡根目录（五个文件夹）
 */
void bsp_ai_show_sd_dir(const char *dir);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_UI_H
