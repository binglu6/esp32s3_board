#!/usr/bin/env python
"""Render the button text '摄像头' into an LVGL RGB565 image descriptor.

Outputs components/bsp/bsp_ai_code/camera_btn_img.h containing a static
lv_image_dsc_t whose RGB565 data matches the little-endian layout this
project's esp_lvgl_port expects (swap_bytes=true).
"""
import os

from PIL import Image, ImageDraw, ImageFont

W, H = 160, 64          # button size in px
RADIUS = 14             # corner radius
SCALE = 4               # supersample factor for anti-aliasing

BG = (255, 255, 255)    # main-screen background (white)
BTN = (33, 150, 243)    # button fill (Material blue)
TEXT_COLOR = (255, 255, 255)  # label color

FONT_PATH = r"C:\Windows\Fonts\simhei.ttf"
FONT_SIZE = 34
LABEL_TEXT = "摄像头"

OUT = r"D:\ESP32_Workspace\esp32s3_board\components\bsp\bsp_ai_code\camera_btn_img.h"

img = Image.new("RGB", (W * SCALE, H * SCALE), BG)
d = ImageDraw.Draw(img)
d.rounded_rectangle([0, 0, W * SCALE - 1, H * SCALE - 1],
                    radius=RADIUS * SCALE, fill=BTN)

font = ImageFont.truetype(FONT_PATH, FONT_SIZE * SCALE)
bbox = d.textbbox((0, 0), LABEL_TEXT, font=font)
tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
d.text(((W * SCALE - tw) / 2 - bbox[0], (H * SCALE - th) / 2 - bbox[1]),
       LABEL_TEXT, font=font, fill=TEXT_COLOR)

img = img.resize((W, H), Image.LANCZOS)
px = img.load()

vals = []
for y in range(H):
    for x in range(W):
        r, g, b = px[x, y][:3]
        v = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)
        vals.append(v)

lines = []
lines.append("// camera_btn_img.h — auto-generated: '摄像头' button bitmap (RGB565)")
lines.append("#ifndef CAMERA_BTN_IMG_H")
lines.append("#define CAMERA_BTN_IMG_H")
lines.append("")
lines.append("#include \"lvgl.h\"")
lines.append("")
lines.append("#define CAMERA_BTN_W (%d)" % W)
lines.append("#define CAMERA_BTN_H (%d)" % H)
lines.append("")
lines.append("static const uint16_t camera_btn_img_data[%d] = {" % (W * H))
per = 16
for i in range(0, len(vals), per):
    chunk = vals[i:i + per]
    lines.append("    " + ", ".join("0x%04x" % v for v in chunk) + ",")
lines.append("};")
lines.append("")
lines.append("static const lv_image_dsc_t camera_btn_img_dsc = {")
lines.append("    .header = {")
lines.append("        .magic = LV_IMAGE_HEADER_MAGIC,")
lines.append("        .cf = LV_COLOR_FORMAT_RGB565,")
lines.append("        .w = %d," % W)
lines.append("        .h = %d," % H)
lines.append("        .stride = %d," % (W * 2))
lines.append("    },")
lines.append("    .data_size = %d," % (W * H * 2))
lines.append("    .data = (const uint8_t *)camera_btn_img_data,")
lines.append("};")
lines.append("")
lines.append("#endif // CAMERA_BTN_IMG_H")
lines.append("")

with open(OUT, "w", encoding="utf-8") as f:
    f.write("\n".join(lines))

print("text bbox: %dx%d (fits %dx%d: %s)" % (tw // SCALE, th // SCALE, W, H,
                                              tw // SCALE <= W and th // SCALE <= H))
print("wrote %s (%d bytes)" % (OUT, os.path.getsize(OUT)))
