# Testing Rules（嵌入式验证）

本项目**没有应用级单元测试框架**（见 CLAUDE.md），验证以「编译 + 烧录实测」为主。

- **编译即冒烟测试**：任何改动后至少运行 `idf.py build`，确认无编译错误与警告，再交付。
- **硬件行为必须实测**：涉及 LCD、摄像头、触摸、引脚电平的功能，需 `idf.py -p COM3 flash monitor` 烧录验证；**不得声称未烧录实测的功能已通过**。
- **组件自带 test_apps 属于上游**：`components/` 下 vendored 组件（esp32-camera、esp_lcd_ili9341 等）内的 `test_apps/` 不在本项目运行；可参考其 pytest 结构，为 `components/bsp/` 编写测试。
- **内存验证**：涉及帧缓冲、PSRAM 分配的改动，调用 `show_heap_sram_psram()` 观察堆水位；确认每条路径都归还相机帧（`esp_camera_fb_return`），无泄漏。
- **任务验证**：多任务改动（`task_process_camera` / `task_process_lcd`）检查栈水位（`uxTaskGetStackHighWaterMark`）与队列是否溢出/丢帧。
- **如需引入单元测试**：使用 ESP-IDF 内置 Unity 集成（在组件下建 `test/` 子目录，通过 `idf.py test` 运行），测试代码同样遵循 code-style 规则。
