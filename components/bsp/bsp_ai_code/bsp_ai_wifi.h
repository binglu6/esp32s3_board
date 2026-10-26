// bsp_ai_wifi.h
#ifndef BSP_AI_WIFI_H
#define BSP_AI_WIFI_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include <stdbool.h>
#include <stddef.h>

#include "bsp_ai_sdcard.h"
#include "esp_err.h"

/** 扫描结果最多保留的 AP 数量 */
#define BSP_AI_WIFI_MAX_AP     (20)
/** SSID 最大字节数（不含结尾 0） */
#define BSP_AI_WIFI_SSID_MAX   (32)
/** 密码最大字节数（不含结尾 0） */
#define BSP_AI_WIFI_PASS_MAX   (64)

/**
 * 初始化 WiFi 协议栈（NVS / netif / 事件循环 / STA）。
 *
 * 只初始化硬件与协议栈，不会自动连接；自动连接由
 * bsp_ai_wifi_auto_connect_start() 在后台完成。
 *
 * @return esp_err_t 成功返回 ESP_OK。
 */
esp_err_t bsp_ai_wifi_init(void);

/**
 * 启动后台自动连接任务：读取 BSP_AI_WIFI_DIR 下保存的网络，按修改时间
 * 从新到旧依次尝试，连上即停止。没有保存记录时什么也不做。
 *
 * @return esp_err_t 成功创建任务返回 ESP_OK。
 */
esp_err_t bsp_ai_wifi_auto_connect_start(void);

/**
 * 请求一次后台扫描（非阻塞）。
 *
 * @return esp_err_t 正在扫描时返回 ESP_ERR_INVALID_STATE。
 */
esp_err_t bsp_ai_wifi_scan_start(void);

/** @return true 表示扫描任务正在运行。 */
bool bsp_ai_wifi_scan_busy(void);

/** @return true 表示至少完成过一次扫描。 */
bool bsp_ai_wifi_scan_done(void);

/** @return 最近一次扫描到的 AP 数量。 */
size_t bsp_ai_wifi_ap_count(void);

/**
 * @param index AP 下标
 * @return SSID 字符串，越界返回 NULL。
 */
const char *bsp_ai_wifi_ap_ssid(size_t index);

/** @return 信号强度 dBm，越界返回 0。 */
int bsp_ai_wifi_ap_rssi(size_t index);

/** @return true 表示该 AP 需要密码。 */
bool bsp_ai_wifi_ap_secure(size_t index);

/** @return true 表示已获取 IP、连接成功。 */
bool bsp_ai_wifi_connected(void);

/** @return true 表示正在连接。 */
bool bsp_ai_wifi_connecting(void);

/** @return 当前已连接 SSID，未连接时返回空字符串。 */
const char *bsp_ai_wifi_current_ssid(void);

/**
 * 请求后台连接指定 AP（非阻塞）。连接成功后自动把 SSID / 密码写入
 * BSP_AI_WIFI_DIR。
 *
 * @param ssid     AP 名称
 * @param password 密码，空字符串表示开放网络
 * @return esp_err_t 参数非法或队列满时返回错误。
 */
esp_err_t bsp_ai_wifi_connect_async(const char *ssid, const char *password);

/**
 * 解析一个 WiFi 凭据文本（SSID=... / PASSWORD=...）。
 *
 * @param path        文本路径
 * @param ssid        输出 SSID 缓冲
 * @param ssid_len    缓冲大小
 * @param password    输出密码缓冲
 * @param password_len 缓冲大小
 * @return esp_err_t 成功返回 ESP_OK，文件不存在或内容非法返回错误。
 */
esp_err_t bsp_ai_wifi_read_file(const char *path, char *ssid, size_t ssid_len,
                                char *password, size_t password_len);

#ifdef __cplusplus
}
#endif

#endif // BSP_AI_WIFI_H
