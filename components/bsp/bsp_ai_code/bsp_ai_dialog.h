// bsp_ai_dialog.h — 通用确认对话框（删除确认等）
#pragma once

#include <stdbool.h>

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** 确认回调，点击“确定/删除”后执行 */
typedef void (*bsp_ai_dialog_confirm_cb_t)(void *ctx);

/**
 * @brief 弹出确认对话框
 *
 * @param title 标题（中文）
 * @param text  正文（文件名等）
 * @param yes   确认按钮文字
 * @param cb    确认后的回调，可为 NULL
 * @param ctx   回调上下文
 */
void bsp_ai_dialog_confirm(const char *title, const char *text, const char *yes,
                           bsp_ai_dialog_confirm_cb_t cb, void *ctx);

#ifdef __cplusplus
}
#endif
