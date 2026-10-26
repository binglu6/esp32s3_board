#include "bsp_ai_code.h"
#include "bsp_ili9341.h"
#include "bsp_gc0308.h"
#include "bsp_lvgl_display.h"
#include "i2c_pca9557.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

void app_main(void)
{
    ESP_ERROR_CHECK(bsp_i2c_init()); // 创建总线
    pca9557_init();                  // 初始化 IO 扩展芯片

    // 创建 UI（主页面、音乐、录音、SD 卡、RGB 灯、摄像头）
    ESP_ERROR_CHECK(bsp_ai_ui_start());

    while (1) {
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
}
