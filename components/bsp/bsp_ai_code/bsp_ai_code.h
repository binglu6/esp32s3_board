// bsp_ai_code.h
#ifndef BSP_AI_CODE_H  // 防止头文件重复包含
#define BSP_AI_CODE_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"

/**
 * AI 生成代码入口：创建「主页面 + 摄像头 + 音乐播放 + SD 卡列表」四个页面。
 *
 * 主页面放置「音乐」「SD 卡」「摄像头」三个按钮：
 *   - 音乐：进入播放页面，支持上一首 / 播放暂停 / 下一首 / 音量；
 *   - SD 卡：进入音乐文件列表页面，点击列表项直接播放；
 *   - 摄像头：全屏预览摄像头画面。
 * 各子页面均提供「返回」按钮回到主页面。音乐文件放在 SD 卡根目录或
 * /sdcard/music 下（mp3 / wav）。
 *
 * 内部完成 LCD、触摸、LVGL、摄像头、SD 卡与 ES8311 音频的初始化。调用前需先
 * 完成 I2C 总线与 PCA9557 扩展芯片初始化（bsp_i2c_init + pca9557_init）。
 *
 * @return esp_err_t 成功返回 ESP_OK，失败返回具体 ESP_ERR_*。
 */
esp_err_t bsp_ai_ui_start(void);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_CODE_H
