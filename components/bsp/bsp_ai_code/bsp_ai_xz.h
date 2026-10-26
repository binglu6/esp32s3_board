// bsp_ai_xz.h — 小智 AI 语音助手（官方云 WebSocket + Opus 半双工）
#ifndef BSP_AI_XZ_H
#define BSP_AI_XZ_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"
#include "lvgl.h"

/** 小智助手状态 */
typedef enum {
    BSP_AI_XZ_WAIT_WIFI = 0, /**< 等待 WiFi */
    BSP_AI_XZ_ACTIVATING,    /**< 正在向小智云端注册/获取配置 */
    BSP_AI_XZ_BINDING,       /**< 等待用户在小智后台绑定激活码 */
    BSP_AI_XZ_READY,         /**< 已连接，可以说话 */
    BSP_AI_XZ_LISTENING,     /**< 正在采集并上传 */
    BSP_AI_XZ_THINKING,      /**< 等待云端回复 */
    BSP_AI_XZ_SPEAKING,      /**< 正在播放 TTS */
    BSP_AI_XZ_ERROR,         /**< 出错 */
} bsp_ai_xz_state_t;

/** 页面刷新用的状态快照 */
typedef struct {
    int state;
    bool ready;
    char status[64];
    char code[16];
    char user[160];
    char reply[320];
} bsp_ai_xz_snapshot_t;

/**
 * 初始化小智助手：启动后台任务，等待 WiFi 后获取激活信息并连接云端。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_xz_init(void);

/**
 * 进入/离开小智页面：进入后自动开始连续对话，离开时停止录音与播放。
 *
 * @param active true 表示页面可见并开始自动对话。
 */
void bsp_ai_xz_set_active(bool active);

/**
 * 中断当前说话/播放并回到可说话状态。
 */
void bsp_ai_xz_stop(void);

/**
 * 读取当前状态快照。
 *
 * @param out 输出结构，不能为 NULL。
 */
void bsp_ai_xz_snapshot(bsp_ai_xz_snapshot_t *out);

/**
 * 创建小智助手页面。
 *
 * @return lv_obj_t * 页面对象。
 */
lv_obj_t *bsp_ai_xz_page_create(void);

/**
 * 页面显示时刷新一次。
 */
void bsp_ai_xz_page_refresh(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_XZ_H
