// bsp_ai_sdcard.h
#ifndef BSP_AI_SDCARD_H
#define BSP_AI_SDCARD_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>

#include "esp_err.h"

/** SD 卡挂载点 */
#define BSP_AI_SD_MOUNT_POINT "/sdcard"
/** SD 卡 1 线 SDMMC 引脚（与 esp32s3_core 板级完全一致） */
#define BSP_AI_SD_CLK         (18)
#define BSP_AI_SD_CMD         (8)
#define BSP_AI_SD_D0          (17)

/** 音乐目录（音乐播放页只扫描此目录） */
#define BSP_AI_MUSIC_DIR      BSP_AI_SD_MOUNT_POINT "/music"
/** 录音目录（录音播放页只扫描此目录） */
#define BSP_AI_REC_DIR        BSP_AI_SD_MOUNT_POINT "/录音文件"
/** RGB 灯配置目录 */
#define BSP_AI_RGB_DIR        BSP_AI_SD_MOUNT_POINT "/rgb led"
/** RGB 灯配置文件 */
#define BSP_AI_RGB_FILE       BSP_AI_RGB_DIR "/rgb.txt"
/** WiFi 凭据目录（每个已连接网络一个 txt 文件） */
#define BSP_AI_WIFI_DIR       BSP_AI_SD_MOUNT_POINT "/WiFi"
/** 语音助手目录（API 配置与合成的回复音频） */
#define BSP_AI_AI_DIR         BSP_AI_SD_MOUNT_POINT "/AI"
/** 语音助手配置文件 */
#define BSP_AI_AI_CONFIG      BSP_AI_AI_DIR "/config.txt"
/** 语音助手合成回复音频 */
#define BSP_AI_AI_REPLY       BSP_AI_AI_DIR "/reply.wav"

/**
 * 挂载 SD 卡到 BSP_AI_SD_MOUNT_POINT，并创建音乐 / 录音 / RGB / WiFi 目录。
 *
 * 使用 1 线 SDMMC 模式，内部上拉。挂载失败时不会格式化卡，只返回错误，
 * 上层可稍后再次调用本函数重试（例如用户插入卡后点击刷新）。
 *
 * @return esp_err_t 成功返回 ESP_OK，失败返回 esp_vfs_fat_sdmmc_mount 的错误码。
 */
esp_err_t bsp_ai_sd_mount(void);

/**
 * 确保音乐、录音、RGB、WiFi、AI 五个目录存在。
 * 目录已存在时不做任何事；SD 卡未挂载时返回 ESP_ERR_INVALID_STATE。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_sd_prepare_dirs(void);

/**
 * @return true 表示 SD 卡已成功挂载。
 */
bool bsp_ai_sd_is_mounted(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_SDCARD_H
