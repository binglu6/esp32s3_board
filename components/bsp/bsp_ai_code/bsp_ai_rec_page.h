// bsp_ai_rec_page.h
#ifndef BSP_AI_REC_PAGE_H
#define BSP_AI_REC_PAGE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

/**
 * 创建录音页面（首次调用时创建，之后复用）。
 *
 * 页面包含开始 / 停止（暂停/继续）/ 结束 / 取消录音与返回按钮，
 * 录音保存为 /sdcard/录音文件/录音_NNN.wav。
 *
 * @return lv_obj_t* 页面 screen，失败返回 NULL。
 */
lv_obj_t *bsp_ai_rec_page_create(void);

/**
 * 刷新录音状态、计时与按钮可用状态。
 */
void bsp_ai_rec_page_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_REC_PAGE_H
