# API Conventions（BSP C 接口）

适配本项目（ESP32-S3 固件，无 HTTP API，接口指 `components/bsp/` 暴露的 C 函数与句柄）的接口约定。

- **命名**：BSP 公共接口统一 `bsp_` 前缀 + snake_case（`bsp_lcd_init`、`bsp_camera_init`、`bsp_lvgl_start`、`bsp_i2c_init`）；非 bsp 层的驱动辅助函数按其功能命名（`pca9557_init`、`lcd_cs` 等）。
- **返回约定**：所有可失败的函数返回 `esp_err_t`；成功返回 `ESP_OK`，失败返回具体 `ESP_ERR_*`（语义对齐 ESP-IDF，不自定义错误码）。
- **错误处理**：调用处使用 `ESP_ERROR_CHECK` / `ESP_RETURN_ON_ERROR` / `ESP_GOTO_ON_ERROR`；失败路径清理已分配资源（参考 `bsp_ili9341.c` 的 `err:` 标签清理 `lcd_panel`/`lcd_io`/SPI 总线）。
- **头文件组织**：每个子模块一个 `.h`，使用 include guard + `#pragma once` + `extern "C"` 包裹（兼容 C++，因 esp32-camera 含 `.cpp` 文件）。
- **句柄封装**：驱动句柄（`lcd_panel`、`lcd_io`、`i2c_bus_handle`）作为不透明句柄（opaque handle）以全局变量暴露或由函数创建，不暴露内部结构体。
- **文档**：公共函数在头文件中注释用途、参数、返回值（C 版 JSDoc）。
- **配置集中**：引脚与常量（I2C/SPI/DVP 引脚、I2C 地址、分辨率等）集中在头文件宏定义中（`bsp_ili9341.h`、`bsp_gc0308.h`、`i2c_pca9557.h`），不在 `.c` 中散落。
