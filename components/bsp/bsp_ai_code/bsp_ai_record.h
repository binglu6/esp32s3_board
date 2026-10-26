// bsp_ai_record.h
#ifndef BSP_AI_RECORD_H
#define BSP_AI_RECORD_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdint.h>

#include "esp_err.h"

/** 录音状态 */
typedef enum {
    BSP_AI_REC_IDLE = 0, /**< 空闲/已取消 */
    BSP_AI_REC_RECORDING, /**< 正在录音 */
    BSP_AI_REC_PAUSED,    /**< 录音暂停 */
    BSP_AI_REC_DONE,      /**< 录音已结束并保存 */
} bsp_ai_rec_state_t;

/**
 * 开始录音：先停止音乐播放并关闭播放设备，再用 ES7210 双麦录制 16kHz/16bit/2ch WAV。
 *
 * 文件保存到 BSP_AI_REC_DIR，名称为「录音_001.wav」递增。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_record_start(void);

/**
 * 暂停录音（停止写入但保持 I2S 运行）。
 */
esp_err_t bsp_ai_record_pause(void);

/**
 * 继续录音。
 */
esp_err_t bsp_ai_record_resume(void);

/**
 * 结束录音并保存 WAV（回填文件头）。
 */
esp_err_t bsp_ai_record_stop(void);

/**
 * 取消录音并删除已写入的临时文件。
 */
esp_err_t bsp_ai_record_cancel(void);

/**
 * @return 当前录音状态。
 */
bsp_ai_rec_state_t bsp_ai_record_state(void);

/**
 * @return 本次录音已录制的秒数。
 */
uint32_t bsp_ai_record_seconds(void);

/**
 * 打开 ES7210 麦克风，供小智语音链路直接读取 PCM（不写 WAV）。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_record_pcm_open(void);

/**
 * 读取交织 PCM 数据。
 *
 * @param buf 目标缓冲。
 * @param len 期望字节数。
 * @param got 实际读取字节数。
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_record_pcm_read(void *buf, size_t len, size_t *got);

/**
 * 读取交织 PCM 数据，并指定最长等待时间。
 *
 * 插话检测需要短超时，避免麦克风暂时无数据时长时间阻塞播音。
 *
 * @param buf 目标缓冲。
 * @param len 期望字节数。
 * @param got 实际读取字节数。
 * @param timeout_ms 最长等待毫秒数。
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_record_pcm_read_timeout(void *buf, size_t len, size_t *got, uint32_t timeout_ms);

/**
 * 关闭 ES7210 麦克风。
 */
void bsp_ai_record_pcm_close(void);

/**
 * @return 最近一次保存成功的 WAV 路径；没有则为空字符串。
 */
const char *bsp_ai_record_last_path(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_RECORD_H
