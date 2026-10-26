# CLAUDE.md

This file provides guidance to Claude Code (claude.ai/code) when working with code in this repository.

## Project Overview

ESP-IDF firmware for an ESP32-S3 board that drives an ILI9341 LCD and a GC0308 camera. The current application is an LVGL v9.3 UI: a **main screen** with a 「摄像头」 button that opens a **camera preview screen**; tapping the preview returns to the main screen. A legacy direct camera→LCD streaming path still exists in `bsp_gc0308`. Code comments are in Chinese.

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

**Windows / Git Bash caveat:** `source $IDF_PATH/export.sh` does **not** work here — the venv lives at `C:\Espressif\tools\python\v6.0.2\venv`, and a persistent `MSYSTEM=MINGW64` makes `idf.py` print a warning and skip its `__main__` block. Use the PowerShell profile wrapper instead:

```bash
powershell.exe -NoProfile -ExecutionPolicy Bypass -Command \
  "& 'C:\Espressif\tools\Microsoft.v6.0.2.PowerShell_profile.ps1' | Out-Null; \
   Remove-Item Env:MSYSTEM -ErrorAction SilentlyContinue; \
   Set-Location 'D:\ESP32_Workspace\esp32s3_board'; \
   Invoke-idfpy -p COM3 build"
```

`idf.py monitor` is long-running; run it in the background and stop it explicitly. The target is already `esp32s3`; do not re-run `set-target`.

There is no application-level unit-test framework. Verification is build + flash + serial monitor (see `.claude/rules/testing.md`).

## Architecture

Standard ESP-IDF layout: `main/` is the entry point, `components/` holds vendored and local components, `managed_components/` holds registry deps, `build/` is generated.

### Boot flow — `main/main.c`

```text
bsp_i2c_init()          // I2C master bus (new driver/i2c_master.h API)
pca9557_init()          // PCA9557 I/O expander
bsp_ai_ui_start()       // LCD + LVGL + camera + two-screen UI (never returns to caller)
```

`bsp_ai_ui_start()` internally calls `bsp_lcd_init()`, `bsp_lvgl_display_init()`, `bsp_camera_init()`, then creates the UI under `lvgl_port_lock()`.

### `components/bsp/` submodules

`components/bsp/CMakeLists.txt` registers six source dirs via `SRC_DIRS`; it depends on `esp_driver_i2c`, `esp_driver_gpio`, `esp_lcd_ili9341`, `esp_driver_ledc`, `esp32-camera`, `lvgl`, `esp_lvgl_port`, `esp_lcd_touch_ft5x06`.

- **`logprint`** — heap debug helper `show_heap_sram_psram()`.
- **`i2c_pca9557`** — PCA9557 expander (addr `0x19`) driving LCD CS, PA enable, camera power-down, SD CS over I2C port 0. Uses `i2c_master_bus_handle_t`; old `driver/i2c.h` code is left commented out.
- **`bsp_ili9341`** — ILI9341 over SPI3_HOST, 320×240 RGB565 (BGR). Exposes global `lcd_panel` / `lcd_io` and backlight PWM via LEDC ch 0.
- **`bsp_gc0308`** — GC0308 over DVP. `bsp_camera_init()` configures the camera only (RGB565, QVGA, PSRAM frame buffers). `app_camera_lcd()` is the legacy direct path: it spawns `task_process_camera` (core 1) and `task_process_lcd` (core 0) linked by a 3-deep frame-pointer queue. **`app_camera_lcd()` is not called by the current `app_main`.**
- **`bsp_lvgl_display`** — `bsp_lvgl_display_init()` registers the ILI9341 display and FT5x06 touch with `esp_lvgl_port` (double draw buffer, 50 lines, `swap_bytes = true`). `bsp_lvgl_start()` is the old demo entry (`lv_demo_music()`), also unused by `app_main`.
- **`bsp_ai_code`** — the current application UI; all AI-generated code goes here.

### The two-screen UI — `bsp_ai_code`

- Screens are created with `lv_obj_create(NULL)` and switched with `lv_screen_load()`; no deletion/recreation.
- Main screen: an `lv_image` displaying the pre-rendered 「摄像头」 bitmap, centered, clickable → loads the camera screen.
- Camera screen: a full-screen `lv_image` (320×240), clickable → loads the main screen.
- An `lv_timer` (33 ms, ~30 fps) is created **paused** and resumed/paused from `LV_EVENT_SCREEN_LOADED` / `LV_EVENT_SCREEN_UNLOADED`, so no frames are grabbed while the main screen is showing.
- Two PSRAM `uint16_t[320*240]` buffers give double buffering. The timer copies the latest camera frame into the inactive buffer, returns the `camera_fb_t` immediately, then swaps `lv_image_set_src()` + `lv_obj_invalidate()`. This avoids tearing while LVGL renders in tiles.
- The timer callback runs inside the LVGL task, so it must **not** call `lvgl_port_lock()` itself.

### RGB565 byte order

The camera produces big-endian RGB565 bytes; `esp_lvgl_port` is configured with `swap_bytes = true`, so LVGL expects little-endian `uint16_t` data internally. `cam_fb_to_lvgl_rgb565()` swaps the bytes while copying. Any new image data (bitmaps, camera buffers) must match this convention. Generated bitmap headers use little-endian `uint16_t` literals.

### Chinese text / CJK fonts

The bundled LVGL CJK fonts (`lv_font_simsun_*_cjk`, `lv_font_source_han_sans_sc_*_cjk`) cover only ~1338 common radicals and **do not contain 「摄」(U+6444) or 「头」(U+5934)**, so `lv_label` cannot render 「摄像头」. Static Chinese text is therefore pre-rendered to an RGB565 bitmap:

```bash
C:/Espressif/tools/python/v6.0.2/venv/Scripts/python.exe .claude/scripts/gen_camera_btn.py
```

The script uses Pillow + `C:\Windows\Fonts\simhei.ttf`, writes `components/bsp/bsp_ai_code/camera_btn_img.h` (`lv_image_dsc_t` + `uint16_t` data), and the UI uses `lv_image_set_src()` instead of a label. Extend this script for additional static Chinese strings. `lv_font_conv` (Node) is not available on this machine.

## Hardware Pin Map

- **I2C (port 0):** SDA = GPIO1, SCL = GPIO2
- **LCD SPI3_HOST:** MOSI = 41, CLK = 40, DC = 42, backlight = 39 (LEDC ch 0); MISO = 38 is declared but unused. **CS and reset are not direct GPIOs** — CS is driven through PCA9557 (`lcd_cs()`), reset is `GPIO_NUM_NC`.
- **Camera DVP:** D0–D7 = 16/11/12/10/15/6/4/9, XCLK = 5, VSYNC = 3, HREF = 46, PCLK = 7, SIOD = 1, SIOC = 2. `pin_sccb_sda = -1` reuses the existing I2C bus.
- Pin macros live in `bsp_ili9341.h`, `bsp_gc0308.h`, and `i2c_pca9557.h`, not in `sdkconfig`.

## Notes / Gotchas

- LCD CS, camera power-down, and SD/PA enables go through the PCA9557 expander — never assume ordinary GPIOs.
- Backlight naming is inverted but behavior is correct: the LEDC channel uses `output_invert = true`, so `bsp_display_backlight_on()` → `brightness_set(0)` = physically full brightness. Do not "fix" the on/off bodies without checking the hardware.
- LVGL is v9 (not v8); differences are guarded with `#if LVGL_VERSION_MAJOR >= 9` in `bsp_lvgl_display.c`. All LVGL calls from non-LVGL tasks must be wrapped in `lvgl_port_lock()` / `lvgl_port_unlock()`.
- The legacy camera/LCD task path passes `camera_fb_t *` pointers through a queue (not copies). Every path that receives a frame must return it with `esp_camera_fb_return()`.
- `.clangd` strips `-f*`/`-m*` flags; clangd reads `build/compile_commands.json`, so build once before relying on IntelliSense.
- Project-specific coding/API/testing rules live in `.claude/rules/` (`code-style.md`, `api-conventions.md`, `testing.md`, `thinking.md`) and override generic defaults.
