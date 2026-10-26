---
description: "构建、烧录并监控 ESP32-S3（COM3）"
argument-hint: "[--no-monitor]"
---

按本项目配置执行发布流程（ESP32-S3，16MB flash，自定义分区表 `partitions-16Mib.csv`，串口 COM3）：

1. **构建**
   ```bash
   idf.py build
   ```
   构建失败则立即停止并报告错误，不要继续烧录。

2. **烧录**
   ```bash
   idf.py -p COM3 flash
   ```

3. **串口监控**（默认执行；传 `--no-monitor` 时跳过）
   ```bash
   idf.py -p COM3 monitor
   ```

说明：

- 这些命令需在 ESP-IDF 环境中运行（ESP-IDF Terminal 或 `export.bat`；在 VSCode 中由 ESP-IDF 扩展提供）。若当前 shell 找不到 `idf.py`，请先激活该环境。
- 目标芯片 `esp32s3`、flash 大小 16MB、分区表已在 `sdkconfig` 中配置，无需重复 `set-target`。
- 烧录前确认 COM3 为实际串口（可在 `.vscode/settings.json` 的 `idf.port` 查看/修改）。
- 若构建目录不存在或 `sdkconfig` 有变更，先用 `idf.py reconfigure`；遇到异常时可 `idf.py fullclean` 后重新构建。
