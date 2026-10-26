// logprint.h
#ifndef LOGPRINT_H  // 防止头文件重复包含
#define LOGPRINT_H

#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "esp_err.h"
#include "esp_log.h"
#include "esp_check.h"
#include "esp_heap_caps.h"

void show_heap_sram_psram(void);

#ifdef __cplusplus
}
#endif

#endif // logprint.h