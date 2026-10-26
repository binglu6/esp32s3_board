# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-IDF firmware for an ESP32-S3 board that drives an ILI9341 LCD, a GC0308 camera, ES8311 speaker, ES7210 dual microphones, a WS2812C RGB LED and an SD card. The current application is an LVGL v9.3 multi-page UI: music player, WAV recorder, SD-card browser, RGB control, WiFi manager, Xiaozhi AI voice assistant and camera preview.

**Read `HANDOFF.md` first**: it contains the full feature list, module map, memory budget, hardware pin table and a symptom→root-cause→fix log for every issue found during development. Code comments are in Chinese.

- **Target chip:** ESP32-S3
- **IDF version:** 6.0.2 (`dependencies.lock`; VSCode `idf.currentSetup` → `d:\esp\v6.0.2\esp-idf`)
- **Flash / port:** 16 MB, partition table `partitions-16Mib.csv` (10 MB factory app + 5168 K spiffs), UART on **COM3**
- **PSRAM:** 8 MB octal, 80 MHz (`CONFIG_SPIRAM_MODE_OCT`)

## Build / Flash / Monitor

Run from an ESP-IDF environment (VSCode ESP-IDF extension, ESP-IDF Terminal, or `export.bat`):

```bash
idf.py build            # compile
idf.py -p COM3 flash    # flash
idf.py -p COM3 monitor  # serial monitor
idf.py fullclean        # clean rebuild
```

**Windows / Git Bash caveat:** `source $IDF_PATH/export.sh` does **not** work here — the venv lives at `C:\Espressif\tools\python\v6.0.2\venv`, and a persistent `MSYSTEM=MINGW64` makes `idf.py` print a warning and skip its `__main__` block. The working-directory path has also moved; use the current checkout path and set the three IDF environment variables explicitly (see `HANDOFF.md` §2.1):

```powershell
$env:IDF_PATH='D:\esp\v6.0.2\esp-idf'
$env:IDF_TOOLS_PATH='C:\Espressif'
$env:IDF_PYTHON_ENV_PATH='C:\Espressif\tools\python\v6.0.2\venv'
$env:IDF_PYTHON_CHECK_CONSTRAINTS='0'
. 'D:\esp\v6.0.2\esp-idf\export.ps1'
Set-Location 'D:\ESP32_Workspace\deepseek_harness_code\esp32s3_board'
idf.py build
```

New source files under `bsp_ai_code` require `idf.py reconfigure` before they are picked up by `SRC_DIRS`.

`idf.py monitor` is long-running; run it in the background and stop it explicitly. The target is already `esp32s3`; do not re-run `set-target`.

There is no application-level unit-test framework. Verification is build + flash + serial monitor (see `.claude/rules/testing.md`).

## Architecture

Standard ESP-IDF layout: `main/` is the entry point, `components/` holds vendored and local components, `managed_components/` holds registry deps, `build/` is generated.

### Boot flow — `main/main.c`

```text
bsp_i2c_init()          // I2C master bus (new driver/i2c_master.h API)
pca9557_init()          // PCA9557 I/O expander
bsp_ai_ui_start()       // LCD + LVGL + camera + audio + RGB + WiFi + AI + multi-page UI (never returns to caller)
```

`bsp_ai_ui_start()` internally calls `bsp_lcd_init()`, `bsp_lvgl_display_init()`, `bsp_camera_init()`, then creates the UI under `lvgl_port_lock()`.

### `components/bsp/` submodules

`components/bsp/CMakeLists.txt` registers `logprint`, `i2c_pca9557`, `bsp_ili9341`, `bsp_gc0308`, `bsp_lvgl_display` and `bsp_ai_code` via `SRC_DIRS`; its `REQUIRES` now also cover `esp_driver_i2s`, `esp_codec_dev`, `esp-audio-player`, `fatfs`, `sdmmc`, `led_strip`, `esp_wifi`, `esp_netif`, `nvs_flash`, `esp_event`, `esp_http_client`, `mbedtls`, `esp_xiaozhi` and `esp_audio_codec`.

- **`logprint`** — heap debug helper `show_heap_sram_psram()`.
- **`i2c_pca9557`** — PCA9557 expander (addr `0x19`) driving LCD CS, PA enable, camera power-down, SD CS over I2C port 0. Uses `i2c_master_bus_handle_t`; old `driver/i2c.h` code is left commented out.
- **`bsp_ili9341`** — ILI9341 over SPI3_HOST, 320×240 RGB565 (BGR). Exposes global `lcd_panel` / `lcd_io` and backlight PWM via LEDC ch 0.
- **`bsp_gc0308`** — GC0308 over DVP. `bsp_camera_init()` configures the camera only (RGB565, QVGA, PSRAM frame buffers). `app_camera_lcd()` is the legacy direct path: it spawns `task_process_camera` (core 1) and `task_process_lcd` (core 0) linked by a 3-deep frame-pointer queue. **`app_camera_lcd()` is not called by the current `app_main`.**
- **`bsp_lvgl_display`** — `bsp_lvgl_display_init()` registers the ILI9341 display and FT5x06 touch with `esp_lvgl_port` (`swap_bytes = true`). For internal-RAM reasons it now uses a **single** draw buffer (`LCD_DRAW_BUFF_DOUBLE 0`) of 10 lines (`LCD_DRAW_BUFF_HEIGHT 10`). `bsp_lvgl_start()` is the old demo entry (`lv_demo_music()`), also unused by `app_main`.
- **`bsp_ai_code`** — the current application UI; all AI-generated code goes here.

### The UI — `bsp_ai_code`

`bsp_ai_code` is the whole application layer; see `HANDOFF.md` §5–§7 for page navigation and module APIs.

- Screens are created once with `lv_obj_create(NULL)` and switched with `lv_screen_load()` via `bsp_ai_show_*()` in `bsp_ai_ui.h`.
- Main screen has 7 pre-rendered 100×64 bitmap buttons: music, recorder, SD card, RGB, AI, WiFi, camera.
- The camera screen keeps a 33 ms `lv_timer`, created **paused**, resumed/paused from `LV_EVENT_SCREEN_LOADED` / `LV_EVENT_SCREEN_UNLOADED`.
- Two PSRAM `uint16_t[320*240]` buffers give double buffering; the timer copies the latest frame into the inactive buffer, returns the `camera_fb_t` immediately, then swaps `lv_image_set_src()` + `lv_obj_invalidate()`.
- The timer callback runs inside the LVGL task, so it must **not** call `lvgl_port_lock()` itself.

### RGB565 byte order

The camera produces big-endian RGB565 bytes; `esp_lvgl_port` is configured with `swap_bytes = true`, so LVGL expects little-endian `uint16_t` data internally. `cam_fb_to_lvgl_rgb565()` swaps the bytes while copying. Any new image data (bitmaps, camera buffers) must match this convention. Generated bitmap headers use little-endian `uint16_t` literals.

### Chinese text / CJK fonts

The LVGL-bundled CJK fonts cover only ~1338 characters, so the project ships its own 16 px bitmap font and pre-rendered button bitmaps (details in `HANDOFF.md` §7.7):

- **Dynamic Chinese text** uses `&bsp_ai_font_cjk_16` (`bsp_ai_code/bsp_ai_font_cjk_16.c/.h`): GB2312 symbol + level-1 + level-2, 7445 glyphs, 1 bpp, 238 KB, `ofs_y = -2`, Montserrat 16 ASCII fallback.
- **Static button captions** (music/recorder/SD/RGB/AI/WiFi/camera) are RGB565 bitmaps generated by `.claude/scripts/gen_main_btns.py` (100×64).
- Regenerate the font with `.claude/scripts/gen_cjk_font.py` (Pillow + `C:\Windows\Fonts\simhei.ttf`, 4× supersampling). Characters outside GB2312 still render as boxes; add code points to the script when needed.

Never set `ofs_y` to a positive value: LVGL computes `y1 = pos.y + (line_height - base_line) - box_h - ofs_y`, so the font needs `-2` to avoid clipping.

## Hardware Pin Map

- **I2C (port 0):** SDA = GPIO1, SCL = GPIO2
- **LCD SPI3_HOST:** MOSI = 41, CLK = 40, DC = 42, backlight = 39 (LEDC ch 0); MISO = 38 is declared but unused. **CS and reset are not direct GPIOs** — CS is driven through PCA9557 (`lcd_cs()`), reset is `GPIO_NUM_NC`.
- **Camera DVP:** D0–D7 = 16/11/12/10/15/6/4/9, XCLK = 5, VSYNC = 3, HREF = 46, PCLK = 7, SIOD = 1, SIOC = 2. `pin_sccb_sda = -1` reuses the existing I2C bus.
- **SD card:** SDMMC 1-line, CLK = 18, CMD = 8, D0 = 17, internal pull-up; CS is driven through PCA9557.
- **Audio:** I2S0 MCLK = 48, BCLK = 21, WS = 14, DOUT = 47, DIN = 13; ES8311 speaker at I2C 0x18 (7-bit), ES7210 dual mic at 0x41 (write address 0x82). PA enable goes through PCA9557.
- **RGB LED:** WS2812C on GPIO45, one LED, GRB order.
- Pin macros live in `bsp_ili9341.h`, `bsp_gc0308.h`, `i2c_pca9557.h`, `bsp_ai_audio.h`, `bsp_ai_record.h` and `bsp_ai_rgb.h`, not in `sdkconfig`.

## Notes / Gotchas

- LCD CS, camera power-down, and SD/PA enables go through the PCA9557 expander — never assume ordinary GPIOs.
- Backlight naming is inverted but behavior is correct: the LEDC channel uses `output_invert = true`, so `bsp_display_backlight_on()` → `brightness_set(0)` = physically full brightness. Do not "fix" the on/off bodies without checking the hardware.
- LVGL is v9 (not v8); differences are guarded with `#if LVGL_VERSION_MAJOR >= 9` in `bsp_lvgl_display.c`. All LVGL calls from non-LVGL tasks must be wrapped in `lvgl_port_lock()` / `lvgl_port_unlock()`.
- The legacy camera/LCD task path passes `camera_fb_t *` pointers through a queue (not copies). Every path that receives a frame must return it with `esp_camera_fb_return()`.
- `.clangd` strips `-f*`/`-m*` flags; clangd reads `build/compile_commands.json`, so build once before relying on IntelliSense.
- **PSRAM stack rule:** any task that may touch flash/NVS/OTA (WiFi credentials, xiaozhi `get_info`, certificate bundle) must use an internal-RAM stack. A PSRAM stack triggers `assert failed: esp_task_stack_is_sane_cache_disabled()`. Opus encode/decode tasks are safe in PSRAM because they never touch flash.
- Internal SRAM is the scarce resource: after boot only ~59 KB free / ~31.7 KB largest block. TLS handshake needs ~31 KB contiguous. See `HANDOFF.md` §4.2 before enlarging any LVGL/WiFi/TLS buffer.
- Project-specific coding/API/testing rules live in `.claude/rules/` (`code-style.md`, `api-conventions.md`, `testing.md`, `thinking.md`) and override generic defaults.
