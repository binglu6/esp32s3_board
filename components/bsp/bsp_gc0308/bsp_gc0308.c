#include "bsp_gc0308.h"

static const char *TAG = "bsp_camera";
// 全局变量声明
volatile bool camera_running = false; // 添加运行标志
SemaphoreHandle_t camera_mutex = NULL; // 添加互斥锁
// 定义lcd显示队列句柄
static QueueHandle_t xQueueLCDFrame = NULL;

// 摄像头硬件初始化
void bsp_camera_init(void)
{
    dvp_pwdn(0); // 打开摄像头

    camera_config_t config;
    config.ledc_channel = LEDC_CHANNEL_1; // LEDC通道选择  用于生成XCLK时钟 但是S3不用
    config.ledc_timer = LEDC_TIMER_0;     // LEDC timer选择  用于生成XCLK时钟 但是S3不用
    config.pin_d0 = CAMERA_PIN_D0;
    config.pin_d1 = CAMERA_PIN_D1;
    config.pin_d2 = CAMERA_PIN_D2;
    config.pin_d3 = CAMERA_PIN_D3;
    config.pin_d4 = CAMERA_PIN_D4;
    config.pin_d5 = CAMERA_PIN_D5;
    config.pin_d6 = CAMERA_PIN_D6;
    config.pin_d7 = CAMERA_PIN_D7;
    config.pin_xclk = CAMERA_PIN_XCLK;
    config.pin_pclk = CAMERA_PIN_PCLK;
    config.pin_vsync = CAMERA_PIN_VSYNC;
    config.pin_href = CAMERA_PIN_HREF;
    config.pin_sccb_sda = -1; // 这里写-1 表示使用已经初始化的I2C接口
    config.pin_sccb_scl = CAMERA_PIN_SIOC;
    config.sccb_i2c_port = 0;
    config.pin_pwdn = CAMERA_PIN_PWDN;
    config.pin_reset = CAMERA_PIN_RESET;
    config.xclk_freq_hz = 20000000;//XCLK_FREQ_HZ
    config.pixel_format = PIXFORMAT_RGB565;
    config.frame_size = FRAMESIZE_QVGA;
    config.jpeg_quality = 12;
    config.fb_count = 2;
    config.fb_location = CAMERA_FB_IN_PSRAM;
    config.grab_mode = CAMERA_GRAB_LATEST; //CAMERA_GRAB_WHEN_EMPTY

    // camera init
    esp_err_t err = esp_camera_init(&config); // 配置上面定义的参数
    if (err != ESP_OK)
    {
        ESP_LOGE(TAG, "Camera init failed with error 0x%x", err);
        return;
    }

    sensor_t *s = esp_camera_sensor_get(); // 获取摄像头型号

    if (s) {
        s->set_framesize(s, FRAMESIZE_QVGA);
        
        if (s->id.PID == GC0308_PID) {
           s->set_hmirror(s, 1); // 这里控制摄像头镜像 写1镜像 写0不镜像
        }
        
        // 设置输出格式为RGB565
        s->set_pixformat(s, PIXFORMAT_RGB565);
    }

    // 验证实际输出分辨率
    camera_fb_t *test_frame = esp_camera_fb_get();
    if (test_frame) {
        ESP_LOGI(TAG, "Actual frame: %dx%d, size=%d, format=%d", 
                 test_frame->width, test_frame->height, 
                 test_frame->len, test_frame->format);
        esp_camera_fb_return(test_frame);
    } else {
        ESP_LOGW(TAG, "Failed to get test frame");
    }

}

// lcd处理任务
static void task_process_lcd(void *arg)
{
    camera_fb_t *frame = NULL;

    while (true)
    {
        if (xQueueReceive(xQueueLCDFrame, &frame, portMAX_DELAY))
        {
            esp_lcd_panel_draw_bitmap(lcd_panel, 0, 0, frame->width, frame->height, (uint16_t *)frame->buf);
            esp_camera_fb_return(frame);
        }
    }
}

// 摄像头处理任务
static void task_process_camera(void *arg)
{
    while (camera_running) {
        camera_fb_t *frame = esp_camera_fb_get();
        if (!frame) {
            ESP_LOGD(TAG, "Get frame timeout");
            vTaskDelay(pdMS_TO_TICKS(10));
            continue;
        }
        
        // 检查帧大小
        size_t expected_size = 320 * 240 * 2; // RGB565
        if (frame->len != expected_size) {
            ESP_LOGW(TAG, "Frame size mismatch: got %d, expected %d", 
                     frame->len, expected_size);
        }
        
        if (xQueueSend(xQueueLCDFrame, &frame, pdMS_TO_TICKS(10)) != pdPASS) {
            ESP_LOGW(TAG, "Queue full, drop frame");
            esp_camera_fb_return(frame);
        }
        
        vTaskDelay(pdMS_TO_TICKS(5));
    }
    vTaskDelete(NULL);
}

// 让摄像头显示到LCD
void app_camera_lcd(void)
{
    // 创建互斥锁
    camera_mutex = xSemaphoreCreateMutex();
    if (!camera_mutex) {
        ESP_LOGE(TAG, "Mutex create failed");
        return;
    }
    
    // 初始化摄像头
    xSemaphoreTake(camera_mutex, portMAX_DELAY);
    bsp_camera_init();
    camera_running = true;
    xSemaphoreGive(camera_mutex);

    xQueueLCDFrame = xQueueCreate(3, sizeof(camera_fb_t *)); // 增加队列长度
    if (xQueueLCDFrame == NULL) {
        ESP_LOGE(TAG, "Queue create failed!");
        return;
    }
    xTaskCreatePinnedToCore(task_process_camera, "task_process_camera", 6 * 1024, NULL, 5, NULL, 1);
    xTaskCreatePinnedToCore(task_process_lcd, "task_process_lcd", 4 * 1024, NULL, 5, NULL, 0);
}



