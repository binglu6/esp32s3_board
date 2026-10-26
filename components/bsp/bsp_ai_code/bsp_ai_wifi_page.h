// bsp_ai_wifi_page.h — WiFi 扫描与连接页面
#pragma once

#ifdef __cplusplus
extern "C" {
#endif

#include "lvgl.h"

lv_obj_t *bsp_ai_wifi_page_create(void);
void bsp_ai_wifi_page_refresh(void);

#ifdef __cplusplus
}
#endif
