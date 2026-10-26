#!/usr/bin/env python
"""Render the 「音乐」 and 「SD 卡」 main-screen buttons into LVGL RGB565 images.

The board's built-in font set has no CJK glyphs (and enabling a CJK font only
covers a small subset), so static Chinese labels are pre-rendered to RGB565
bitmaps, following the same convention as gen_camera_btn.py:

    pixel = ((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3)

written as little-endian uint16_t, which matches esp_lvgl_port(swap_bytes=true).
"""
import os

from PIL import Image, ImageDraw, ImageFont

W, H = 160, 64          # same size as the existing camera button
RADIUS = 14
SCALE = 4               # supersample factor for anti-aliasing

FONT_PATH = r"C:\Windows\Fonts\simhei.ttf"

OUT_DIR = os.path.join(
    os.path.dirname(os.path.dirname(os.path.dirname(os.path.abspath(__file__)))),
    "components", "bsp", "bsp_ai_code")

# (output, guard, prefix, label, font size, top color, bottom color)
BUTTONS = [
    ("music_btn_img.h", "MUSIC_BTN_IMG_H", "music", "音乐", 36,
     (30, 215, 96), (18, 161, 80)),
    ("sd_btn_img.h", "SD_BTN_IMG_H", "sd", "SD 卡", 30,
     (255, 183, 77), (245, 124, 0)),
]


def render(spec):
    out_name, guard, prefix, label, font_size, top, bottom = spec

    img = Image.new("RGB", (W * SCALE, H * SCALE), top)
    draw = ImageDraw.Draw(img)

    # vertical gradient fill, clipped to the rounded rectangle
    mask = Image.new("L", (W * SCALE, H * SCALE), 0)
    ImageDraw.Draw(mask).rounded_rectangle(
        [0, 0, W * SCALE - 1, H * SCALE - 1], radius=RADIUS * SCALE, fill=255)
    for y in range(H * SCALE):
        t = y / (H * SCALE - 1)
        color = tuple(int(top[i] + (bottom[i] - top[i]) * t) for i in range(3))
        ImageDraw.Draw(img).line([(0, y), (W * SCALE, y)], fill=color)
    base = Image.new("RGB", (W * SCALE, H * SCALE), (255, 255, 255))
    base.paste(img, (0, 0), mask)
    img = base

    draw = ImageDraw.Draw(img)
    font = ImageFont.truetype(FONT_PATH, font_size * SCALE)
    bbox = draw.textbbox((0, 0), label, font=font)
    tw, th = bbox[2] - bbox[0], bbox[3] - bbox[1]
    draw.text(((W * SCALE - tw) / 2 - bbox[0], (H * SCALE - th) / 2 - bbox[1]),
              label, font=font, fill=(255, 255, 255))

    img = img.resize((W, H), Image.LANCZOS)
    px = img.load()

    vals = []
    for y in range(H):
        for x in range(W):
            r, g, b = px[x, y][:3]
            vals.append(((r & 0xF8) << 8) | ((g & 0xFC) << 3) | (b >> 3))

    lines = [
        "// %s — auto-generated: '%s' button bitmap (RGB565)" % (out_name, label),
        "#ifndef %s" % guard,
        "#define %s" % guard,
        "",
        '#include "lvgl.h"',
        "",
        "#define %s_BTN_W (%d)" % (prefix.upper(), W),
        "#define %s_BTN_H (%d)" % (prefix.upper(), H),
        "",
        "static const uint16_t %s_btn_img_data[%d] = {" % (prefix, W * H),
    ]
    for i in range(0, len(vals), 16):
        lines.append("    " + ", ".join("0x%04x" % v for v in vals[i:i + 16]) + ",")
    lines += [
        "};",
        "",
        "static const lv_image_dsc_t %s_btn_img_dsc = {" % prefix,
        "    .header = {",
        "        .magic = LV_IMAGE_HEADER_MAGIC,",
        "        .cf = LV_COLOR_FORMAT_RGB565,",
        "        .w = %d," % W,
        "        .h = %d," % H,
        "        .stride = %d," % (W * 2),
        "    },",
        "    .data_size = %d," % (W * H * 2),
        "    .data = (const uint8_t *)%s_btn_img_data," % prefix,
        "};",
        "",
        "#endif // %s" % guard,
        "",
    ]

    path = os.path.join(OUT_DIR, out_name)
    with open(path, "w", encoding="utf-8") as f:
        f.write("\n".join(lines))
    print("text bbox: %dx%d (fits %dx%d: %s)" % (
        tw // SCALE, th // SCALE, W, H,
        tw // SCALE <= W and th // SCALE <= H))
    print("wrote %s (%d bytes)" % (path, os.path.getsize(path)))


for spec in BUTTONS:
    render(spec)
