// bsp_ai_audio.h
#ifndef BSP_AI_AUDIO_H
#define BSP_AI_AUDIO_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "driver/i2s_std.h"
#include "esp_err.h"

/** 最多缓存的曲目数 */
#define BSP_AI_AUDIO_MAX_TRACKS (32)

/** 音频列表音源：音乐目录与录音目录互不混播 */
typedef enum {
    BSP_AI_AUDIO_SRC_MUSIC = 0, /**< 只扫描 /sdcard/music 下的 mp3/wav */
    BSP_AI_AUDIO_SRC_RECORD,    /**< 只扫描 /sdcard/录音文件 下的 wav */
} bsp_ai_audio_src_t;

/**
 * 初始化 SD 卡、ES8311 播放通道、I2S 全双工通道与音频播放器。
 *
 * SD 卡挂载失败不会阻止初始化：此时曲目列表为空，但 SD 卡页面仍可正常进入，
 * 用户插入卡后可通过页面上的刷新按钮重新扫描。
 *
 * 必须在 bsp_i2c_init() 与 pca9557_init() 之后调用。
 *
 * @return esp_err_t 音频播放通道初始化成功返回 ESP_OK；SD 卡失败不影响返回值。
 */
esp_err_t bsp_ai_audio_init(void);

/**
 * 选择音源并重新扫描对应目录；音源变化时会先停止当前播放。
 *
 * @param src 音乐或录音音源
 * @return esp_err_t 找到文件返回 ESP_OK，否则返回 ESP_ERR_NOT_FOUND。
 */
esp_err_t bsp_ai_audio_select_source(bsp_ai_audio_src_t src);

/**
 * @return 当前音源。
 */
bsp_ai_audio_src_t bsp_ai_audio_source(void);

/**
 * 按当前音源重新扫描 SD 卡目录，并按文件名排序。
 * 若当前曲目仍然存在，会保持当前选中项不变。
 *
 * @return esp_err_t 找到文件返回 ESP_OK，否则返回 ESP_ERR_NOT_FOUND。
 */
esp_err_t bsp_ai_audio_rescan(void);

/**
 * @return 当前扫描到的曲目数量。
 */
size_t bsp_ai_audio_count(void);

/**
 * @param index 曲目下标
 * @return 不带扩展名的显示名称；下标越界返回 NULL。
 */
const char *bsp_ai_audio_name(size_t index);

/**
 * @param index 曲目下标
 * @return 文件的完整路径；下标越界返回 NULL。
 */
const char *bsp_ai_audio_path(size_t index);

/**
 * @return 当前选中曲目下标（无曲目时为 0）。
 */
size_t bsp_ai_audio_current(void);

/**
 * @return true 表示正在播放。
 */
bool bsp_ai_audio_is_playing(void);

/**
 * @return true 表示本次上电后已经播放过至少一首（用于区分「暂停」与「未开始」）。
 */
bool bsp_ai_audio_started(void);

/**
 * 播放指定曲目（从文件开头开始）。
 *
 * @param index 曲目下标
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_play(size_t index);

/**
 * 播放指定路径（必须已在当前曲目列表中）。
 *
 * @param path 完整路径
 * @return esp_err_t 成功返回 ESP_OK，未找到返回 ESP_ERR_NOT_FOUND。
 */
esp_err_t bsp_ai_audio_play_path(const char *path);

/**
 * 播放任意路径的音频文件（不要求出现在当前曲目列表中）。
 *
 * 用于播放 AI 合成的回复；播放结束后停止，不会自动切歌或循环。
 *
 * @param path 完整路径（mp3/wav）
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_play_file(const char *path);

/**
 * 停止播放并关闭播放设备，释放 I2S 输出通道以便录音复用。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_stop(void);

/**
 * 播放/暂停切换：未开始时播放当前曲目，播放中暂停，暂停中继续。
 */
esp_err_t bsp_ai_audio_toggle(void);

/**
 * 切换到下一首并开始播放（列表循环）。
 */
esp_err_t bsp_ai_audio_next(void);

/**
 * 切换到上一首并开始播放（列表循环）。
 */
esp_err_t bsp_ai_audio_prev(void);

/**
 * 设置喇叭音量。
 *
 * @param volume 0~100，越界值会被截断。
 */
void bsp_ai_audio_set_volume(int volume);

/**
 * @return 当前音量 0~100。
 */
int bsp_ai_audio_volume(void);

/**
 * 以指定采样格式打开喇叭，用于播放云端返回的 PCM 数据（小智语音）。
 *
 * @param rate     采样率，例如 24000。
 * @param channels 声道数，例如 1。
 * @param bits     位宽，例如 16。
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_pcm_open(uint32_t rate, uint8_t channels, uint8_t bits);

/**
 * 向已打开的 PCM 播放通道写入数据。
 *
 * @param data PCM 数据。
 * @param len  字节数。
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_pcm_write(const void *data, size_t len);

/**
 * 临时静音喇叭，用于插话检测时排除扬声器回声。
 *
 * @param mute true 静音，false 恢复。
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_pcm_mute(bool mute);

/**
 * 关闭 PCM 播放通道并关闭功放。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_audio_pcm_close(void);

/**
 * @return 录音复用的 I2S 接收通道句柄；不可用时返回 NULL。
 */
i2s_chan_handle_t bsp_ai_audio_rx_handle(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_AUDIO_H
