# Code Style Rules（C / ESP-IDF）

适配本项目（ESP32-S3 嵌入式 C 固件）的代码风格规范。

- **严格编译**：保持 ESP-IDF 默认 `-Wall -Werror`（`sdkconfig` 中不放松），避免隐式类型转换、未使用变量、符号重定义；不要用 `void*` 绕过类型，除非驱动句柄强制要求——尽量使用 ESP-IDF 强类型句柄（`esp_lcd_panel_handle_t`、`i2c_master_bus_handle_t`、`camera_fb_t` 等）。
- **函数长度不超过 40 行**，超出则拆分为更小的函数（对应 C 中的职责拆分）。
- **优先 `const` 与 `static`**：只读数据/指针加 `const`；仅本文件使用的函数、变量加 `static`，避免污染全局命名空间（如 `static const char *TAG`、`static QueueHandle_t xQueueLCDFrame`）。
- **include 顺序**：标准库头 → FreeRTOS / ESP-IDF 头 → 项目本地头（`bsp_*.h`）。本地头按依赖由底层到上层。
- **公共函数注释**：所有在头文件中暴露的函数，在头文件内注释用途、参数、返回值（C 版 JSDoc；可参考 `bsp_ili9341.h` 中的写法）。
- **禁止 `printf` / `cout`**：统一使用 ESP_LOG（`ESP_LOGI` / `ESP_LOGE` / `ESP_LOGW` / `ESP_LOGD`），每个源文件定义 `static const char *TAG`；内存调试使用项目 `logprint.h` 的 `show_heap_sram_psram()`。
