// bsp_ai_rgb_page.c — RGB 灯调色页面（R/G/B + 亮度 + 保存）
#include "bsp_ai_rgb_page.h"

#include <stdio.h>

#include "bsp_ai_font_cjk_16.h"
#include "bsp_ai_rgb.h"
#include "bsp_ai_ui.h"
#include "esp_log.h"

static const char *TAG = "bsp_ai_rgb_page";

#define COLOR_BG        (0x0E1116)
#define COLOR_CARD      (0x1A2129)
#define COLOR_CARD_DOWN (0x2A3542)
#define COLOR_TEXT      (0xF2F5F7)
#define COLOR_DIM       (0x8A97A5)
#define COLOR_ACCENT    (0x1ED760)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_red_slider = NULL;
static lv_obj_t *s_green_slider = NULL;
static lv_obj_t *s_blue_slider = NULL;
static lv_obj_t *s_bright_slider = NULL;
static lv_obj_t *s_red_value = NULL;
static lv_obj_t *s_green_value = NULL;
static lv_obj_t *s_blue_value = NULL;
static lv_obj_t *s_bright_value = NULL;
static lv_obj_t *s_preview = NULL;
static lv_obj_t *s_status = NULL;

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_card_button(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD_DOWN), LV_STATE_PRESSED);
    lv_obj_set_style_radius(btn, 8, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    return btn;
}

static lv_obj_t *make_slider(lv_obj_t *parent, int x, int y, int width, int max, int value,
                             lv_event_cb_t cb)
{
    lv_obj_t *slider = lv_slider_create(parent);
    lv_obj_set_pos(slider, x, y);
    lv_obj_set_size(slider, width, 10);
    lv_slider_set_range(slider, 0, max);
    lv_slider_set_value(slider, value, LV_ANIM_OFF);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_CARD_DOWN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(slider, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(slider, cb, LV_EVENT_VALUE_CHANGED, NULL);
    return slider;
}

static void update_value_labels(int r, int g, int b, int brightness)
{
    char buf[8];
    snprintf(buf, sizeof(buf), "%d", r);
    lv_label_set_text(s_red_value, buf);
    snprintf(buf, sizeof(buf), "%d", g);
    lv_label_set_text(s_green_value, buf);
    snprintf(buf, sizeof(buf), "%d", b);
    lv_label_set_text(s_blue_value, buf);
    snprintf(buf, sizeof(buf), "%d%%", brightness);
    lv_label_set_text(s_bright_value, buf);

    lv_color_t color = lv_color_make((uint8_t)(r * brightness / 100),
                                     (uint8_t)(g * brightness / 100),
                                     (uint8_t)(b * brightness / 100));
    lv_obj_set_style_bg_color(s_preview, color, 0);
}

static void apply_from_sliders(void)
{
    int r = (int)lv_slider_get_value(s_red_slider);
    int g = (int)lv_slider_get_value(s_green_slider);
    int b = (int)lv_slider_get_value(s_blue_slider);
    int brightness = (int)lv_slider_get_value(s_bright_slider);

    (void)bsp_ai_rgb_set((uint8_t)r, (uint8_t)g, (uint8_t)b, (uint8_t)brightness);
    update_value_labels(r, g, b, brightness);
}

static void slider_changed_cb(lv_event_t *e)
{
    (void)e;
    apply_from_sliders();
}

static void save_click_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = bsp_ai_rgb_save();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "save rgb config failed: %s", esp_err_to_name(err));
    }
    lv_label_set_text(s_status, err == ESP_OK ? "已保存" : "保存失败");
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    (void)bsp_ai_rgb_save();
    bsp_ai_show_main();
}

static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *back = make_card_button(parent, 6, 6, 74, 30);
    make_label(back, "返回", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(back, 0));
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = make_label(parent, "RGB 灯", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *save = make_card_button(parent, 240, 6, 74, 30);
    make_label(save, "保存", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(save, 0));
    lv_obj_add_event_cb(save, save_click_cb, LV_EVENT_CLICKED, NULL);
}

static void create_channel_row(lv_obj_t *parent, const char *name, uint32_t color, int y,
                               lv_obj_t **slider, lv_obj_t **value, uint8_t init)
{
    lv_obj_t *label = make_label(parent, name, &lv_font_montserrat_16, color);
    lv_obj_set_pos(label, 12, y - 2);

    *slider = make_slider(parent, 46, y, 196, 255, init, slider_changed_cb);

    *value = make_label(parent, "0", &lv_font_montserrat_16, COLOR_DIM);
    lv_obj_set_width(*value, 46);
    lv_obj_set_pos(*value, 250, y - 2);
    lv_obj_set_style_text_align(*value, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_rows(lv_obj_t *parent)
{
    create_channel_row(parent, "R", 0xE5484D, 48, &s_red_slider, &s_red_value,
                       bsp_ai_rgb_red());
    create_channel_row(parent, "G", 0x1ED760, 84, &s_green_slider, &s_green_value,
                       bsp_ai_rgb_green());
    create_channel_row(parent, "B", 0x3B82F6, 120, &s_blue_slider, &s_blue_value,
                       bsp_ai_rgb_blue());

    lv_obj_t *bright = make_label(parent, "亮度", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_set_pos(bright, 12, 154);
    s_bright_slider = make_slider(parent, 60, 158, 182, 100, bsp_ai_rgb_brightness(),
                                  slider_changed_cb);
    s_bright_value = make_label(parent, "0%", &lv_font_montserrat_16, COLOR_DIM);
    lv_obj_set_width(s_bright_value, 46);
    lv_obj_set_pos(s_bright_value, 250, 154);
    lv_obj_set_style_text_align(s_bright_value, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_preview(lv_obj_t *parent)
{
    s_preview = lv_obj_create(parent);
    lv_obj_remove_style_all(s_preview);
    lv_obj_set_pos(s_preview, 12, 192);
    lv_obj_set_size(s_preview, 64, 38);
    lv_obj_set_style_radius(s_preview, 8, 0);
    lv_obj_set_style_bg_opa(s_preview, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(s_preview, 2, 0);
    lv_obj_set_style_border_color(s_preview, lv_color_hex(COLOR_CARD_DOWN), 0);

    s_status = make_label(parent, "返回时自动保存", &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_set_pos(s_status, 88, 202);
}

lv_obj_t *bsp_ai_rgb_page_create(void)
{
    if (s_screen != NULL) {
        return s_screen;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    create_top_bar(s_screen);
    create_rows(s_screen);
    create_preview(s_screen);
    bsp_ai_rgb_page_sync();
    return s_screen;
}

void bsp_ai_rgb_page_sync(void)
{
    if (s_screen == NULL) {
        return;
    }
    int r = bsp_ai_rgb_red();
    int g = bsp_ai_rgb_green();
    int b = bsp_ai_rgb_blue();
    int brightness = bsp_ai_rgb_brightness();

    lv_slider_set_value(s_red_slider, r, LV_ANIM_OFF);
    lv_slider_set_value(s_green_slider, g, LV_ANIM_OFF);
    lv_slider_set_value(s_blue_slider, b, LV_ANIM_OFF);
    lv_slider_set_value(s_bright_slider, brightness, LV_ANIM_OFF);
    update_value_labels(r, g, b, brightness);
}
