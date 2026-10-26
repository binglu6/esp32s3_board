---
description: "定位并修复用户描述的问题，然后验证构建"
argument-hint: "<问题描述>"
---

你是本项目（ESP32-S3 摄像头→LCD + LVGL）的调试者。用户报告的问题：

> $ARGUMENTS

按以下步骤处理：

1. **定位**：依据 [CLAUDE.md](CLAUDE.md) 的架构，判断问题落在 `main/main.c` 还是 `components/bsp/` 的某个子模块（`logprint` / `i2c_pca9557` / `bsp_ili9341` / `bsp_gc0308` / `bsp_lvgl_display`）。
2. **复现推理**：先阅读相关源码并给出根因判断，再动手修改；没有证据时不要猜测硬件引脚或寄存器值。
3. **修改**：用最小改动修复，风格与周边代码一致（中文注释、ESP 错误宏、既有命名习惯）。
4. **验证**：能构建时运行 `idf.py build` 确认无编译错误；涉及需烧录才能验证的硬件行为时，明确告知用户需要 `idf.py flash monitor` 实测，**不要声称已修复未验证的部分**。

注意本项目常见坑（见 CLAUDE.md）：PCA9557 扩展芯片控制 CS/电源、相机帧缓冲所有权、LVGL v9 API 差异、PSRAM 分配。

完成后总结：根因 → 改动文件与行 → 验证结果 → 遗留风险。
