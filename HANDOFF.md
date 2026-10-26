# ESP32-S3 开发板固件交接文档

> 适用工程：`esp32s3_board`（ESP-IDF 6.0.2 / ESP32-S3 / 16 MB Flash / 8 MB PSRAM）
> 最后更新：2026-10-26，对应提交 `e84e302`
> 新对话开始时建议先读本文件，再读 `CLAUDE.md` 与 `.claude/rules/`。
> 本文记录的是「当前已实现并上板验证」的行为；未验证项会单独标注。

---

## 1. 项目速览

一块 320×240 ILI9341 触摸屏 + GC0308 摄像头 + ES8311 喇叭 + ES7210 双麦 + WS2812C RGB 灯 + SD 卡的 ESP32-S3 开发板，固件提供七个功能入口：

| 主页面按钮 | 页面 | 核心能力 |
| --- | --- | --- |
| 音乐 | 音乐播放页 | 只播放 `/sdcard/music` 下的 mp3/wav；上一首/下一首/暂停/播放/音量 |
| 录音 | 录音页 | ES7210 双麦录 WAV 到 `/sdcard/录音文件`；开始/暂停/继续/结束/取消 |
| SD 卡 | SD 卡浏览器 | 根目录四个文件夹：`music`、`录音文件`、`rgb led`、`WiFi`；可进目录看文件、看 RGB/WiFi 文本、删除录音 |
| RGB 灯 | RGB 调色页 | R/G/B + 亮度滑条，实时预览，保存到 SD；上电恢复 |
| AI 助手 | 小智语音页 | 小智官方云（`esp_xiaozhi`）；激活码绑定；进入页面自动连续对话；Opus 编解码 |
| WiFi | WiFi 页 | 扫描附近 AP、屏幕键盘输密码、连接、保存到 SD；上电自动连接；SD 页可查看 SSID/密码 |
| 摄像头 | 摄像头页 | 320×240 预览，点击返回 |

所有子页面都有「返回」；录音播放页与音乐播放页是两套独立页面，音源互不混播。

- 芯片：ESP32-S3（QFN56，rev v0.2），双核 240 MHz，8 MB OCT PSRAM @80 MHz
- Flash：16 MB，`partitions-16Mib.csv`（10 MB factory app + 5168 K spiffs）
- 串口：**COM3**，115200
- LVGL：v9.3，RGB565，`swap_bytes = true`
- Git：`https://github.com/binglu6/esp32s3_board`，分支 `main`

---

## 2. 构建 / 烧录 / 监视

### 2.1 本机可用的 PowerShell 命令

ESP-IDF 在 `D:\esp\v6.0.2\esp-idf`，工具与 Python 在 `C:\Espressif`。`export.ps1` 默认去 `C:\Espressif` 找 constraints 会失败，需要显式给三个环境变量：

```powershell
$env:IDF_PATH='D:\esp\v6.0.2\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\tools\python\v6.0.2\venv'
$env:IDF_PYTHON_CHECK_CONSTRAINTS='0'
. 'D:\esp\v6.0.2\esp-idf\export.ps1' | Out-Null
Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue
Set-Location 'D:\ESP32_Workspace\deepseek_harness_code\esp32s3_board'

idf.py build
idf.py -p COM3 flash
idf.py -p COM3 monitor     # 长跑，放后台；烧录前先停掉，否则 COM3 busy
```

也可以使用 `C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1` + `Invoke-idfpy`（见 `CLAUDE.md`）。

### 2.2 常见构建问题

- **新增 `.c` 后链接报 `undefined reference`**：`SRC_DIRS "bsp_ai_code"` 在 configure 阶段展开，新文件不会被自动发现。执行一次 `idf.py reconfigure` 再 build（曾出现在 `bsp_ai_dialog.c`）。
- **`Could not open COM3, the port is busy`**：后台 `idf.py monitor` 还占着串口，先结束该任务。
- **build 目录里残留旧工程路径**（例如从 `D:\ESP32_Workspace\esp32s3_board` 搬过来）：`idf.py fullclean` 后重建。
- 工程开启 `-Wall -Werror`：`snprintf` 可能截断会直接报错，路径缓冲要按 `PATH_LEN + NAME_LEN + 2` 给足，或用 `strncpy` 手动补 `\0`。
- 当前固件约 **2.38 MB**，10 MB app 分区剩余约 77%。

### 2.3 烧录后必看的启动日志

正常启动应依次出现：

```text
SD card mounted
bsp_ai_audio: ... tracks=10
bsp_ai_rgb: loaded R=... G=... B=... brightness=...
wifi ready
bsp_ai_xz: chat ready
```

若 `bsp_ai_xz` 停在 `waiting wifi`，先看 WiFi 是否连上；若输出 `please bind` / 激活码，说明设备还没在小智后台绑定。

---

## 3. 目录结构

```text
esp32s3_board/
├── main/
│   ├── main.c                     # app_main：I2C → PCA9557 → bsp_ai_ui_start()
│   ├── idf_component.yml          # esp_xiaozhi ^0.1.2、esp_audio_codec ~2.5.0
│   └── certs/server_chain.pem     # 小智服务器中间证书（自定义 bundle）
├── components/
│   ├── bsp/
│   │   ├── logprint/              # show_heap_sram_psram()
│   │   ├── i2c_pca9557/           # IO 扩展：LCD CS / PA / 摄像头 PWDN / SD CS
│   │   ├── bsp_ili9341/           # LCD + 背光
│   │   ├── bsp_gc0308/            # 摄像头
│   │   ├── bsp_lvgl_display/      # LVGL 显示/触摸移植层
│   │   └── bsp_ai_code/           # ★ 所有业务代码
│   ├── esp_codec_dev/             # 官方组件（含 ES8311 / ES7210 驱动）
│   ├── esp-audio-player/          # 官方组件（mp3/wav 解码）
│   ├── esp-libhelix-mp3/          # mp3 解码库
│   ├── led_strip/                 # WS2812 驱动（RMT/SPI 后端）
│   ├── esp_lcd_ili9341/ esp_lcd_touch_ft5x06/ esp_lvgl_port/ esp32-camera/ lvgl/
├── managed_components/            # esp_xiaozhi、esp_audio_codec、cjson、websocket、mqtt、mcp…
├── partitions-16Mib.csv
├── sdkconfig                      # 已提交，含所有关键内存/TLS/WiFi 配置
├── CLAUDE.md
├── HANDOFF.md                     # 本文件
└── .claude/
    ├── rules/                     # 代码风格、API、测试规则
    └── scripts/                   # 字库/按钮位图/证书抓取脚本
```

`components/bsp/CMakeLists.txt` 的 `REQUIRES` 已包含：`esp_driver_i2c/gpio/ledc/i2s`、`esp_codec_dev`、`esp-audio-player`、`fatfs`、`sdmmc`、`led_strip`、`esp_wifi`、`esp_netif`、`nvs_flash`、`esp_event`、`esp_http_client`、`mbedtls`、`esp_xiaozhi`、`esp_audio_codec`。

---

## 4. 启动流程与内存

### 4.1 `main/main.c`

```c
ESP_ERROR_CHECK(bsp_i2c_init());   // 新 i2c_master 驱动，SDA=1 SCL=2
pca9557_init();                    // LCD CS / PA / DVP PWDN / SD CS
ESP_ERROR_CHECK(bsp_ai_ui_start()); // 不返回，内部建 UI
```

`bsp_ai_ui_start()`（`bsp_ai_code.c`）顺序：

```text
bsp_lcd_init()
bsp_lvgl_display_init()
bsp_camera_init()
bsp_ai_audio_init()      // 挂载 SD、ES8311、I2S 全双工、mp3/wav 播放器
bsp_ai_rgb_init()        // WS2812 + 读取 rgb.txt
bsp_ai_wifi_init()       // NVS/netif/STA
bsp_ai_wifi_auto_connect_start()
bsp_ai_xz_init()         // 后台任务：等 WiFi → 取激活信息 → WebSocket
lvgl_port_lock()
  分配摄像头双缓冲（PSRAM 2 × 153.6 KB）
  创建主页面 + 摄像头页面
  创建 33 ms 抓帧定时器（暂停）
  lv_screen_load(main)
lvgl_port_unlock()
show_heap_sram_psram()
```

### 4.2 内存预算（非常重要）

这块板子内部 SRAM 很紧，小智 + TLS + LVGL 同时存在时只剩约 **30 KB 最大连续块**：

| 项目 | 当前值 | 说明 |
| --- | --- | --- |
| `CONFIG_LV_MEM_SIZE_KILOBYTES` | **40** | 原 64，为 TLS 让内存 |
| `LCD_DRAW_BUFF_DOUBLE` | **0** | 单缓冲，省 6.4 KB |
| `LCD_DRAW_BUFF_HEIGHT` | **10** | 原 50；摄像头页刷新会受一点影响但可接受 |
| `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` | **4096** | 小智 Opus 帧很小；若换回大音频接口需 16384 |
| `CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN` | 2048 | |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | **4096** | 大于 4 KB 的分配优先走 PSRAM |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | y | WiFi/LWIP 缓冲放 PSRAM |
| WiFi 静态 RX/TX | 6 / 6 | 开了 TRY_ALLOCATE 后动态 TX 不可用 |
| WiFi 动态 RX / cache TX | 16 / 16 | |
| 启动后 SRAM | 空闲 ≈ 59 KB，最大块 ≈ 31.7 KB | TLS 握手至少需要 ≈ 31 KB 连续块 |

小智相关任务：

| 任务 | 栈 | 位置 | 说明 |
| --- | --- | --- | --- |
| `xz_task` | 12 KB | **内部 SRAM** | 会调用 `get_info`（NVS/flash），绝不能用 PSRAM 栈 |
| `xz_opus_task` | 32 KB | PSRAM | 只做 Opus 编码 |
| `xz_dec_task` | 32 KB | PSRAM | 只做 Opus 解码 + PCM 写 |
| WebSocket / MCP | 组件默认 | 内部 | |

> **硬约束：任何可能访问 flash/NVS/OTA 的任务都不能把栈放在 PSRAM。**
> 否则会触发 `assert failed: spi_flash_disable_interrupts_caches_and_other_cpu
> (esp_task_stack_is_sane_cache_disabled())`。

---

## 5. 页面与导航

页面对象全部由 `lv_obj_create(NULL)` 创建，`lv_screen_load()` 切换，**只创建一次**。统一入口在 `bsp_ai_ui.h`：

```c
void bsp_ai_show_main(void);
void bsp_ai_show_camera(void);
void bsp_ai_show_music(void);
void bsp_ai_show_rec(void);
void bsp_ai_show_rec_player(void);
void bsp_ai_show_rgb(void);
void bsp_ai_show_sd(void);
void bsp_ai_show_wifi(void);
void bsp_ai_show_ai(void);
void bsp_ai_show_sd_dir(const char *dir);  // NULL = 根目录
```

主页面 7 个按钮全部是**预渲染 RGB565 位图**（100×64），坐标：

```text
music  (6,6)      rec (110,6)     sd (214,6)
rgb    (6,80)     ai  (110,80)    wifi (214,80)
camera (110,154)
```

- 位图由 `.claude/scripts/gen_main_btns.py` 生成，输出 `music_btn_img.h`、`rec_btn_img.h`、`sd_btn_img.h`、`rgb_btn_img.h`、`ai_btn_img.h`、`wifi_btn_img.h`、`camera_btn_img.h`。
- 像素格式：little-endian `uint16_t`，`((r&0xF8)<<8)|((g&0xFC)<<3)|(b>>3)`，配合 `swap_bytes=true`。
- 按钮按下效果：`LV_STATE_PRESSED` 时 `LV_OPA_70`。

### 5.1 页面返回关系

```text
主页面 ──音乐──> 音乐页 ──返回/列表──> 主页面 / SD(music)
主页面 ──录音──> 录音页 ──返回──> 主页面
主页面 ──SD──> SD 根 ──music──> 文件列表 ──点击歌曲──> 音乐页
                     ──录音文件──> 文件列表 ──点击 wav──> 录音播放页
                     ──rgb led──> 文件列表 ──点击 txt──> RGB 信息页
                     ──WiFi──> 文件列表 ──点击 txt──> WiFi 信息页
主页面 ──RGB──> RGB 页 ──返回（自动保存）──> 主页面
主页面 ──WiFi──> WiFi 页 ──返回──> 主页面
主页面 ──AI──> 小智页 ──返回（停止对话）──> 主页面
主页面 ──摄像头──> 摄像头页 ──点击──> 主页面
```

SD 信息页的返回目标保存在 `s_info_parent`，不要再硬编码成 RGB 目录。

---

## 6. SD 卡目录约定

挂载点 `/sdcard`，1 线 SDMMC（CLK=18、CMD=8、D0=17，内部上拉），**挂载失败不格式化**，`max_files=8`，FATFS 使用 UTF-8 编码（`CONFIG_FATFS_API_ENCODING_UTF_8=y`、LFN heap）。

| 路径 | 内容 | 生产者 |
| --- | --- | --- |
| `/sdcard/music/` | mp3 / wav 音乐 | 用户拷贝 |
| `/sdcard/录音文件/` | `录音_001.wav` … 16 kHz / 2ch / 16bit | 录音模块 |
| `/sdcard/rgb led/rgb.txt` | `R=…` `G=…` `B=…` `BRIGHTNESS=…` 四行 | RGB 模块 |
| `/sdcard/WiFi/<SSID>.txt` | `SSID=…` `PASSWORD=…` 两行 | WiFi 连接成功后 |
| `/sdcard/AI/` | 旧版硅基流动方案的 config/reply，**当前未使用**，仅自动建目录 | — |

WiFi 文件名会清洗 `/\:*?"<>|` 与控制字符为 `_`，去尾部空格/点，空名用 `wifi`；同名 SSID 复用文件，不同 SSID 重名追加 `_2`、`_3`。

> 注意：WiFi 密码是**明文**保存在 SD 卡里的，这是产品需求，不是遗漏。

---

## 7. 功能模块

### 7.1 音频播放 `bsp_ai_audio.c/.h`

- I2S0 全双工：TX + RX，16 kHz / 32 bit / 2ch；MCLK=48、BCLK=21、WS=14、DOUT=47、DIN=13。
- ES8311 通过 `esp_codec_dev`（DAC 模式，`use_mclk=true`，PA 由 PCA9557 的 `pa_en()` 控制）。
- 播放器：`esp-audio-player`，配置 `write_fn=bsp_i2s_write`、`clk_set_fn=bsp_codec_set_fs`、优先级 6、core 1。
- 音源枚举：

```c
typedef enum {
    BSP_AI_AUDIO_SRC_MUSIC = 0,  // 只扫 /sdcard/music，mp3/wav
    BSP_AI_AUDIO_SRC_RECORD,     // 只扫 /sdcard/录音文件，wav
} bsp_ai_audio_src_t;
```

- 关键 API：`select_source()`、`play_path()`、`play_file()`、`stop()`、`toggle()`、`next()`、`prev()`、`rescan()`、`rx_handle()`，以及小智用的 `pcm_open/write/mute/close`。
- 音乐列表循环、自动下一首；录音音源播完即停、不循环、不自动切歌。
- **`bsp_ai_audio_stop()` 关闭播放设备后会重新 `i2s_channel_enable(s_tx_chan)`**，保持 MCLK/BCLK/WS 输出，否则 ES7210 没有时钟、录音全是 `-1`。
- ES8311 设备采用「懒重开」：`codec_write()` 发现 `s_dev_open=false` 时先 `i2s_channel_disable(TX)` 再 `esp_codec_dev_open(&s_dev_fs)`，避免停止/删除后再播放无声。
- `audio_player` 启动时会 `mute_fn(MUTE)`，所以 **PCM 直通路径必须在 `bsp_ai_audio_pcm_open()` 里显式 `set_out_mute(false)`**。
- 音量 0~100，`bsp_ai_audio_set_volume()`。

### 7.2 录音 `bsp_ai_record.c/.h`

- ES7210 双麦，I2C 7 位地址 **0x41 → 8 位写地址 0x82**；`CONFIG_CODEC_ES7210_SUPPORT=y`。
- 16 kHz / 16 bit / 2ch，MIC1+MIC2，30 dB，`esp_codec_dev` 打开后 **延时 300 ms** 等芯片稳定。
- 复用 `bsp_ai_audio_rx_handle()` 的 I2S RX；录音前必须 `bsp_ai_audio_stop()` 关闭播放设备（I2S 数据通道共享，采样率/位宽冲突会 `Mode 0 conflict`）。
- 写 WAV：先写 44 字节占位头，结束时 `fseek(0)` 回填 `data_size`；文件名 `录音_%03u.wav`，用 `stat` 找空号。
- 后台任务 core 1、优先级 5、栈 4096；暂停时继续 `i2s_channel_read` 并丢弃数据；取消则删除文件。
- 页面 `bsp_ai_rec_page.c`：状态卡 + 开始/停止(暂停↔继续)/结束/取消 + 返回，200 ms 刷新；离开时若在录会先保存。
- 删除录音：SD 列表每行右侧垃圾桶 + 录音播放页右上垃圾桶，均弹确认框（`bsp_ai_dialog`）。

### 7.3 SD 卡浏览器 `bsp_ai_sd_page.c`

- 三种视图：`VIEW_ROOT`、`VIEW_FILES`、`VIEW_INFO`。
- 根目录固定四项：`music`、`录音文件`、`rgb led`、`WiFi`。
- 过滤规则：music 目录只显示 mp3/wav；录音目录只显示 wav；rgb/WiFi 只显示 txt。
- 点音频：先 `bsp_ai_audio_select_source()`，再 `play_path()`，再进对应播放页。
- 点 `rgb.txt` / WiFi txt：进信息页；RGB 显示色块 + R/G/B + 亮度，WiFi 显示 `SSID:` 与 `密码:`。
- 录音行支持删除；删除当前正在播放的文件前会先 `stop()`。
- 列表最多 24 项（`MAX_ENTRIES`），超出不显示。

### 7.4 RGB 灯 `bsp_ai_rgb.c` + `bsp_ai_rgb_page.c`

- WS2812C，1 颗，数据脚 **GPIO45**，`led_strip` SPI 后端（`SPI2_HOST`、DMA），颜色顺序 GRB。
- 默认颜色 `(0,120,255)`、亮度 70%。
- `rgb.txt` 四行：`R=`、`G=`、`B=`、`BRIGHTNESS=`；亮度按百分比缩放 RGB。
- 页面：R/G/B 三条 0~255 滑条 + 亮度 0~100% + 颜色预览 + 保存；返回时自动保存；上电 `bsp_ai_rgb_init()` 自动读取。

### 7.5 WiFi `bsp_ai_wifi.c` + `bsp_ai_wifi_page.c`

- `nvs_flash_init`（处理 `NO_FREE_PAGES` / `NEW_VERSION` 后 erase 重试）、`esp_netif_init`、默认事件循环、STA。
- 扫描任务 core 0、优先级 3、栈 4096；主动扫描最多 32 条，按 SSID 去重保留最强 RSSI，按 RSSI 降序最多 20 个。
- 连接任务 core 0、优先级 4、栈 4096；队列投递；先 disconnect + 200 ms，再 `esp_wifi_connect()`，等 15 s；成功后保存凭据。
- 自动连接任务 `wifi_auto` 延迟 2.5 s，扫描 `/sdcard/WiFi/*.txt` 按 mtime 新→旧最多 8 个逐个尝试，连上即停；**任务结束必须 `vTaskDelete(NULL)`**。
- 页面：顶栏返回 /「WiFi 连接」/「扫描」；列表显示信号图标与 RSSI；点加密 AP 弹密码框 + LVGL 键盘，开放 AP 直接连；状态行显示「未连接 / 正在连接 / 已连接」。
- 屏幕键盘要点：`lv_keyboard` 构造时默认 `LV_ALIGN_BOTTOM_MID`，设置坐标必须用 `lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0)`；用 `lv_obj_set_pos()` 会在对齐结果上叠加偏移，键盘会跑到屏幕外。

### 7.6 小智 AI `bsp_ai_xz.c` + `bsp_ai_xz_page.c`

接入方式：**小智官方云**（`espressif/esp_xiaozhi`），不是直连 DeepSeek Key。

- 设备启动后 `esp_xiaozhi_chat_get_info()` 向 OTA 端点（默认 `https://api.tenclass.net/xiaozhi/ota/`）上报板级信息，拿 WebSocket 配置与激活码。
- 未绑定时云端返回 `Please log in to the console to add this device and enter the verification code: xxxxxx.`，`parse_binding_prompt()` 解析 6 位码显示在页面；用户到 **xiaozhi.me** 添加设备并输入激活码。
- **对话模型在 xiaozhi.me 后台选择 `deepseek-ai/DeepSeek-V4.1-Flash-1017`**；固件不保存任何 Key。
- 状态机：`WAIT_WIFI → ACTIVATING → BINDING → READY → LISTENING → THINKING → SPEAKING`。
- 页面为连续对话：进入页面 `bsp_ai_xz_set_active(true)` 后自动循环「聆听 → 上传 → 等回复 → 播放 TTS → 再聆听」，不需要按键；离开页面停止。
- 上行：16 kHz 单声道 Opus / 60 ms 一帧，`esp_opus_enc` 在 `xz_opus_task`（32 KB PSRAM 栈）里编码。
- 下行：服务端 Opus 常见 24 kHz，`xz_dec_task`（32 KB PSRAM 栈）解码，线性插值重采样到 16 kHz，复制左右声道后经 `bsp_ai_audio_pcm_write()` 写 ES8311。
- 解码 TTS 帧先进入 128 KB PSRAM 流缓冲（`XZ_DEC_SB_SIZE`），避免服务端突发导致丢帧。
- TTS 等待上限 300 秒（长歌/长回复不截断）；PCM 已关闭时解码任务直接丢帧，不再刷 `dec failed`。
- WebSocket 空闲约 60~66 秒会被服务端断开；`xz_chat_event_handler()` 监听 `CONNECTED/DISCONNECTED`，用 `XZ_LINK_BIT` + `reopen_channel()` 自动关闭/重开音频通道，页面短暂显示「连接断开，正在重连...」。
- `open_audio_channel()` 前必须等 WebSocket `CONNECTED`（15 s 超时），否则 hello 发送失败。
- TLS：服务器只下发 leaf 证书，中间证书在 `main/certs/server_chain.pem`，`CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y`、路径 `"main/certs"`。换服务器/证书过期时用 `.claude/scripts/fetch_aia.py` 重新抓 AIA 中间证书。
- 历史说明：`bsp_ai_ai.c/.h`（硅基流动直连方案）已删除；`/sdcard/AI` 目录与 `BSP_AI_AI_*` 宏是残留，可清理。

### 7.7 中文字体与按钮位图

内置 LVGL CJK 字体只有约 1338 字，业务文案大量缺字，因此：

1. **动态中文**（歌曲名、文件名、WiFi 名、状态文字）使用自制 16 px 点阵字体：

```c
LV_FONT_DECLARE(bsp_ai_font_cjk_16);
lv_obj_set_style_text_font(label, &bsp_ai_font_cjk_16, 0);
```

   - 源文件：`bsp_ai_font_cjk_16.c/.h`
   - 覆盖 GB2312 符号区 + 一级 + 二级汉字，共 **7445 字**，1 bpp，每字 32 字节，位图 **238,240 字节**
   - `line_height=18`、`base_line=3`、`ofs_y=-2`、ASCII 回退 `lv_font_montserrat_16`
   - 生成脚本：`.claude/scripts/gen_cjk_font.py`（Pillow + `C:\Windows\Fonts\simhei.ttf`，4× 超采样）
   - 新增字若不在 GB2312（生僻字/emoji）仍会显示方框；需要时改脚本按码点补字。

2. **静态按钮文字**（音乐/录音/SD 卡/RGB 灯/AI 助手/WiFi/摄像头）预渲染成 RGB565 位图，脚本 `.claude/scripts/gen_main_btns.py`，尺寸 100×64。

> `ofs_y` 必须是 `-2`。LVGL 的基线公式是
> `y1 = pos.y + (line_height - base_line) - box_h - ofs_y`，
> 设为 `1` 会让字形整体上移 2 px 被裁掉。

---

## 8. 引脚与外设

| 外设 | 引脚 / 地址 | 备注 |
| --- | --- | --- |
| I2C | SDA=1, SCL=2 | 新 `i2c_master` 驱动，100 kHz |
| PCA9557 | 0x19 | LCD_CS=BIT0、PA_EN=BIT1、DVP_PWDN=BIT2、SD_CS=BIT3 |
| LCD ILI9341 | SPI3：MOSI=41、CLK=40、DC=42、BL=39 | CS 走 PCA9557；复位 NC；背光 LEDC ch0，`output_invert=true`（duty 0 = 全亮） |
| 触摸 FT5x06 | I2C 0x38 | `swap_xy=1`、`mirror_y=1` |
| 摄像头 GC0308 | D0–D7=16/11/12/10/15/6/4/9，XCLK=5、VSYNC=3、HREF=46、PCLK=7 | SCCB 复用 I2C |
| SD 卡 | CLK=18、CMD=8、D0=17 | 1 线 SDMMC，内部上拉 |
| I2S / ES8311 | MCLK=48、BCLK=21、WS=14、DOUT=47、DIN=13 | 16 kHz，ES8311 DAC |
| ES7210 | I2C 0x41（写 0x82） | 双麦，16 kHz/16 bit/2ch，30 dB，MIC1+MIC2 |
| WS2812C | GPIO45 | 1 颗，GRB |

---

## 9. 关键 `sdkconfig` 项

```text
CONFIG_LV_MEM_SIZE_KILOBYTES=40
CONFIG_LV_FONT_MONTSERRAT_20=y
CONFIG_LV_FONT_MONTSERRAT_24=y
CONFIG_FATFS_API_ENCODING_UTF_8=y
CONFIG_FATFS_LFN_HEAP=y
CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN=4096
CONFIG_MBEDTLS_SSL_OUT_CONTENT_LEN=2048
CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE=y
CONFIG_MBEDTLS_CUSTOM_CERTIFICATE_BUNDLE_PATH="main/certs"
CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL=4096
CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP=y
CONFIG_ESP_WIFI_STATIC_RX_BUFFER_NUM=6
CONFIG_ESP_WIFI_DYNAMIC_RX_BUFFER_NUM=16
CONFIG_ESP_WIFI_STATIC_TX_BUFFER_NUM=6
CONFIG_ESP_WIFI_CACHE_TX_BUFFER_NUM=16
CONFIG_CODEC_ES8311_SUPPORT=y
CONFIG_CODEC_ES7210_SUPPORT=y
```

`LCD_DRAW_BUFF_DOUBLE 0`、`LCD_DRAW_BUFF_HEIGHT 10` 在 `components/bsp/bsp_lvgl_display/bsp_lvgl_display.h`。

> 改这些内存参数时一定成组考虑：LVGL 池、绘制缓冲、TLS 输入缓冲、WiFi 缓冲，任何一项变大都可能让 TLS 握手失败（`-0x008D` / PSA `-141`）。

---

## 10. 问题与修复记录

| # | 现象 | 根因 | 修复 |
| --- | --- | --- | --- |
| 1 | 中文 label 显示方框/缺字 | 内置 CJK 字体只覆盖约 1338 字 | 自制 GB2312 16 px 点阵字体 `bsp_ai_font_cjk_16` |
| 2 | 自制字体字形整体上移、被裁 | `ofs_y=1` 与 LVGL 基线公式不符 | 改为 `ofs_y=-2` |
| 3 | 「谪」「仙」仍缺字 | 只生成 GB2312 一级字库 | 扩展到二级（6763 字） |
| 4 | 状态文字里「，」显示方框 | 字库未包含 GB2312 符号区 | 范围扩到 0xA1–0xF7（7445 字） |
| 5 | WiFi 密码弹窗没有键盘 | `lv_keyboard` 默认 `BOTTOM_MID` 对齐，`set_pos` 变成偏移，y=248 出屏 | `lv_obj_align(kb, LV_ALIGN_BOTTOM_MID, 0, 0)`，并补「取消」按钮 |
| 6 | 录音 WAV 几乎全是 `-1`、偶发满量程 | 停止播放时 `esp_codec_dev_close` 同时关了 I2S TX，ES7210 失去 MCLK/BCLK | `bsp_ai_audio_stop()` 关闭设备后重新 `i2s_channel_enable(s_tx_chan)` |
| 7 | 录音开头有爆音/瞬态 | ES7210 上电未稳定 | `rec_dev_open()` 后 `vTaskDelay(300 ms)` |
| 8 | `ES7210: Write register fail` / `rec_dev_open` 失败 | 用了 0x80，实际 8 位地址是 0x82 | `.addr = 0x82` |
| 9 | 删除录音后再播放无声、自动跳歌 | `stop()` 关闭了 ES8311，播放器不会再调 `clk_set_fn` | 增加 `s_dev_open/s_dev_fs`，`codec_write()` 懒重开设备 |
| 10 | PCM/TTS 写入成功但完全无声 | `audio_player` 启动时把 ES8311 静音，PCM 路径没解除 | `bsp_ai_audio_pcm_open()` 里 `set_out_mute(false)` |
| 11 | 小智 TTS 只出开头几个字（硅基流动时期） | `MBEDTLS_SSL_IN_CONTENT_LEN=4096`，16 KB 音频 TLS 记录被判 `MBEDTLS_ERR_SSL_INVALID_RECORD` | 当时改 16384；换小智后帧很小，再改回 4096 |
| 12 | WiFi 初始化 `Expected to init 10 rx buffer, actual is 3` / `ESP_ERR_NO_MEM` | LVGL 双缓冲 + 64 KB 池 + WiFi 静态缓冲吃光内部 RAM | LVGL 池 40 KB、单缓冲 10 行、WiFi 缓冲调小、`SPIRAM_TRY_ALLOCATE_WIFI_LWIP` |
| 13 | `FreeRTOS Task "wifi_auto" should not return` | 自动连接任务分支直接 return | 分支与末尾 `vTaskDelete(NULL)` |
| 14 | 小智启动崩溃 `esp_task_stack_is_sane_cache_disabled` | `xz_task` 用 PSRAM 栈，却调用 NVS/flash | 改内部 12 KB 栈 |
| 15 | 说话时 `LoadProhibited`，Saved PC `silk_NSQ_c` / `silk_encode_pulses` | Opus 编码需要约 18.7 KB 栈，10/16/20 KB 都溢出 | 独立 `xz_opus_task`，32 KB PSRAM 栈 |
| 16 | TTS 回调里崩溃（WebSocket 任务栈溢出） | Opus 解码在 WS 回调里执行 | 独立 `xz_dec_task` + 128 KB PSRAM 流缓冲 |
| 17 | `No matching trusted root certificate found` | 服务器只下发 leaf，不下发中间证书 | `main/certs/server_chain.pem` + 自定义 bundle，`fetch_aia.py` 抓 AIA |
| 18 | `mbedtls_ssl_setup returned -0x008D` / PSA `-141` | 内部 RAM 不足，TLS 需要 ≈31 KB 连续块 | LVGL 池降到 40 KB；不要同时开 16 KB TLS 输入缓冲 |
| 19 | `Failed to send hello message` / `open audio channel failed` | `chat_start()` 异步，WS 还没连上就开通道 | 等 `ESP_XIAOZHI_CHAT_EVENT_CONNECTED`（15 s）再开 |
| 20 | 空闲约 1 分钟后小智不再响应 | 服务端断开 WS，重连后没有重新 hello/开通道 | `XZ_LINK_BIT` + `reopen_channel()` 自动恢复 |
| 21 | 长歌/长回复播到 30 秒突然没声 | `run_turn()` TTS 等待上限 30 s，提前关 PCM，服务端还在推流 | 上限改 300 s；PCM 关闭时解码任务直接丢帧 |
| 22 | 连续对话第二句只出一部分 | 16 KB 解码流缓冲被服务端突发填满 | 128 KB PSRAM + 800 ms 静音判定 + 丢帧统计 |
| 23 | 插话打断误触发、回复被截断、出现「嗯」 | 扬声器回声被当成人声；静音探测也误触发 | **已回退**，当前不支持语音打断，TTS 播完自动进入下一轮聆听 |
| 24 | 新增 `.c` 后 `undefined reference` | `SRC_DIRS` 在 configure 时展开 | `idf.py reconfigure` |
| 25 | 烧录报 COM3 busy | 后台串口监视未退出 | 结束监视任务再烧 |
| 26 | 旧 build 指向别的工程路径 | build 缓存 | `idf.py fullclean` |
| 27 | 启动偶发 `LoadProhibited` @ `esp_sha_dma`/`gdma_disconnect` | 证书签名校验时 GDMA 竞态（仅出现一次） | 未处理；重启正常，若复现优先查 mbedTLS 硬件 SHA 与并发 TLS |

---

## 11. 已知限制与待办

1. **不支持语音打断**：TTS 播放期间麦克风不参与上行，必须等播完；此前的回声/静音探测方案均因误触发被回退。
2. **小智模型在后端配置**：固件只负责激活与音频链路；换模型要去 xiaozhi.me 后台改，不能写死在代码里。
3. WiFi 仅 2.4 GHz；凭据明文存 SD 卡。
4. 小智 WebSocket 空闲约 60 s 会被服务端断开，重连有 5 s 退避，页面会短暂显示重连状态。
5. `/sdcard/AI` 与 `BSP_AI_AI_*`、`bsp_ai_audio_pcm_mute()` 是旧方案残留，可清理。
6. 启动时证书校验偶发 GDMA panic（仅一次，未复现）。
7. 录音文件为 16 kHz/2ch/16bit，未做降噪/AGC；安静环境下电平较低属正常。
8. SD 文件列表上限 24 项，未做分页/滚动加载。

---

## 12. 二次开发指南

### 12.1 新增一个主页面按钮 + 子页面

1. 在 `.claude/scripts/gen_main_btns.py` 的 `BUTTONS` 里加一项（文件名、guard、前缀、中文、字号、渐变色），运行脚本生成 `xxx_btn_img.h`。
2. 在 `bsp_ai_code.c` 中 `#include "xxx_btn_img.h"`，加 `s_xxx_screen`、`main_xxx_cb()`、`bsp_ai_show_xxx()`。
3. 新页面用 `lv_obj_create(NULL)` 创建，放在 `bsp_ai_code` 下独立 `.c/.h`，提供 `page_create()` 与 `page_refresh()`。
4. 若新增 `.c`，先 `idf.py reconfigure` 再 build。
5. 页面内所有中文用 `&bsp_ai_font_cjk_16`；静态大标题建议走位图。

### 12.2 新增中文文案

- 先确认字在 GB2312 内（常用字基本都在）；动态文本直接 `lv_label` + `&bsp_ai_font_cjk_16`。
- 生僻字：改 `gen_cjk_font.py` 的字表（按 Unicode 码点补），重新生成，注意 `.c` 体积会增大。
- 静态按钮：改 `gen_main_btns.py` 重生成位图。

### 12.3 改音频/采样率

- 上行/下行采样率常量在 `bsp_ai_xz.c`：`XZ_*_RATE`；改下行后要同步改重采样目标与 `pcm_open()`。
- 录音固定 16 kHz/2ch/16bit，改格式要同时改 WAV 头、`rec_dev_open()` 的 `esp_codec_dev_sample_info_t` 和时长计算。
- 换大音频接口（非 Opus）时，把 `CONFIG_MBEDTLS_SSL_IN_CONTENT_LEN` 调回 16384，并确认内部 RAM 还能腾出 ≈31 KB 连续块。

### 12.4 调内存

优先级：LVGL 池 → 绘制缓冲 → WiFi 缓冲 → TLS 缓冲。每次只动一项，烧录后看启动日志的 `SRAM : [free / max / ...]`，`max` 低于约 31 KB 时 TLS 很可能失败。

### 12.5 代码规范

- 中文注释、`ESP_LOGx`（禁止 `printf`/`cout`）、每个 `.c` 定义 `static const char *TAG`。
- 公共函数用 `esp_err_t`，头文件写 C 风格 JSDoc，`extern "C"` + include guard。
- 函数尽量 ≤40 行，常量用宏集中在头文件。
- 规则详见 `.claude/rules/code-style.md`、`api-conventions.md`、`testing.md`。

---

## 13. 排障速查

| 日志/现象 | 先查 |
| --- | --- |
| `SD card mounted` 不出现 | SD 卡座、`BSP_AI_SD_*` 引脚、PCA9557 的 SD_CS、卡是否为 FAT32 |
| 音乐列表空 | `/sdcard/music` 是否有 mp3/wav；点 SD 页刷新 |
| 录音全 `-1` / 无数据 | TX 时钟是否被关（`bsp_ai_audio_stop` 后是否重新 enable）、ES7210 0x82、300 ms 延时 |
| 播放无声但 `write=ESP_OK` | `set_out_mute(false)`、PA_EN、音量、I2S 采样率是否匹配 |
| WiFi 连不上 | 2.4 GHz、密码、`/sdcard/WiFi` 文件内容；看 `reason=` |
| 小智一直 `waiting wifi` | WiFi 是否拿到 IP |
| 小智显示激活码 | 到 xiaozhi.me 绑定；绑定后重启 |
| 小智 `TLS` 失败 | 证书 bundle、系统时间、内部 RAM 最大块、`IN_CONTENT_LEN` |
| 小智连上但没声音 | 解码任务日志 `dec ... write=`、`pcm_open`、静音、采样率（24k→16k） |
| 小智说几句就停 | TTS 等待上限、解码流缓冲是否满、PCM 是否被提前 close |
| 启动 panic 在 `spi_flash_disable...` | 是否有任务用 PSRAM 栈访问 flash/NVS |
| 编译 `undefined reference` | `idf.py reconfigure` |

---

## 14. 本次对话新增内容清单（便于追溯）

- 音乐播放页 + 音乐/SD 卡主页面入口 + 独立音源（music / 录音文件）
- SD 卡文件夹浏览器（四目录、文件列表、RGB/WiFi 信息页、录音删除）
- ES7210 双麦录音（WAV、暂停/继续/结束/取消、删除、独立录音播放页）
- WS2812C RGB 调色与持久化
- WiFi 扫描/键盘输密码/连接/凭据保存/上电自动连接/信息查看
- 小智官方云语音助手（激活码、连续对话、Opus 编解码、TLS 自定义证书、断线重连）
- 自制 GB2312 中文字库与 7 个主页面按钮位图
- 内存裁剪与稳定性修复（LVGL/TLS/WiFi/任务栈）
- Git 仓库初始化并推送到 GitHub

> 更早的摄像头页面、LCD/LVGL 移植、PCA9557 等基础能力见 `CLAUDE.md`。
