// bsp_ai_rec_page.c — 录音页面（开始 / 停止 / 结束 / 取消 / 返回）
#include "bsp_ai_rec_page.h"

#include <stdbool.h>
#include <stdio.h>

#include "bsp_ai_font_cjk_16.h"
#include "bsp_ai_record.h"
#include "bsp_ai_ui.h"
#include "esp_log.h"

static const char *TAG = "bsp_ai_rec_page";

#define COLOR_BG        (0x0E1116)
#define COLOR_CARD      (0x1A2129)
#define COLOR_CARD_DOWN (0x2A3542)
#define COLOR_TEXT      (0xF2F5F7)
#define COLOR_DIM       (0x8A97A5)
#define COLOR_GREEN     (0x1ED760)
#define COLOR_ORANGE    (0xF5A623)
#define COLOR_BLUE      (0x3B82F6)
#define COLOR_RED       (0xE5484D)
#define REFRESH_PERIOD  (200)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_time_label = NULL;
static lv_obj_t *s_start_btn = NULL;
static lv_obj_t *s_stop_btn = NULL;
static lv_obj_t *s_stop_label = NULL;
static lv_obj_t *s_finish_btn = NULL;
static lv_obj_t *s_cancel_btn = NULL;
static lv_timer_t *s_timer = NULL;

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, const char *text, uint32_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, 148, 46);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(COLOR_CARD_DOWN), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_40, LV_STATE_DISABLED);
    lv_obj_set_style_radius(btn, 10, 0);
    lv_obj_set_style_shadow_width(btn, 0, 0);
    lv_obj_set_style_border_width(btn, 0, 0);
    make_label(btn, text, &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(btn, 0));
    return btn;
}

static void set_disabled(lv_obj_t *btn, bool disabled)
{
    if (disabled) {
        lv_obj_add_state(btn, LV_STATE_DISABLED);
    } else {
        lv_obj_remove_state(btn, LV_STATE_DISABLED);
    }
}

static void start_click_cb(lv_event_t *e)
{
    (void)e;
    esp_err_t err = bsp_ai_record_start();
    if (err != ESP_OK) {
        ESP_LOGW(TAG, "start recording failed: %s", esp_err_to_name(err));
        lv_label_set_text(s_state_label, "启动失败");
        return;
    }
    bsp_ai_rec_page_refresh();
}

static void stop_click_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_ai_record_state() == BSP_AI_REC_PAUSED) {
        (void)bsp_ai_record_resume();
    } else {
        (void)bsp_ai_record_pause();
    }
    bsp_ai_rec_page_refresh();
}

static void finish_click_cb(lv_event_t *e)
{
    (void)e;
    (void)bsp_ai_record_stop();
    bsp_ai_rec_page_refresh();
}

static void cancel_click_cb(lv_event_t *e)
{
    (void)e;
    (void)bsp_ai_record_cancel();
    bsp_ai_rec_page_refresh();
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_rec_state_t state = bsp_ai_record_state();
    if (state == BSP_AI_REC_RECORDING || state == BSP_AI_REC_PAUSED) {
        (void)bsp_ai_record_stop();
    }
    bsp_ai_show_main();
}

static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *back = make_button(parent, 8, 6, "返回", COLOR_CARD);
    lv_obj_set_size(back, 74, 30);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = make_label(parent, "录音", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);
}

static void create_status_card(lv_obj_t *parent)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, 8, 44);
    lv_obj_set_size(card, 304, 74);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);

    s_state_label = make_label(card, "就绪", &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_align(s_state_label, LV_ALIGN_TOP_MID, 0, 8);

    s_time_label = make_label(card, "00:00", &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_align(s_time_label, LV_ALIGN_BOTTOM_MID, 0, -6);
}

static void create_buttons(lv_obj_t *parent)
{
    s_start_btn = make_button(parent, 8, 126, "开始录音", COLOR_GREEN);
    lv_obj_add_event_cb(s_start_btn, start_click_cb, LV_EVENT_CLICKED, NULL);

    s_stop_btn = make_button(parent, 164, 126, "停止录音", COLOR_ORANGE);
    s_stop_label = lv_obj_get_child(s_stop_btn, 0);
    lv_obj_add_event_cb(s_stop_btn, stop_click_cb, LV_EVENT_CLICKED, NULL);

    s_finish_btn = make_button(parent, 8, 180, "结束录音", COLOR_BLUE);
    lv_obj_add_event_cb(s_finish_btn, finish_click_cb, LV_EVENT_CLICKED, NULL);

    s_cancel_btn = make_button(parent, 164, 180, "取消录音", COLOR_RED);
    lv_obj_add_event_cb(s_cancel_btn, cancel_click_cb, LV_EVENT_CLICKED, NULL);
}

static void rec_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bsp_ai_rec_page_refresh();
}

static void screen_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        lv_timer_resume(s_timer);
        bsp_ai_rec_page_refresh();
    } else if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOADED) {
        lv_timer_pause(s_timer);
    }
}

lv_obj_t *bsp_ai_rec_page_create(void)
{
    if (s_screen != NULL) {
        return s_screen;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    create_top_bar(s_screen);
    create_status_card(s_screen);
    create_buttons(s_screen);

    s_timer = lv_timer_create(rec_timer_cb, REFRESH_PERIOD, NULL);
    if (s_timer == NULL) {
        ESP_LOGE(TAG, "create refresh timer failed");
        return NULL;
    }
    lv_timer_pause(s_timer);

    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_SCREEN_UNLOADED, NULL);
    return s_screen;
}

void bsp_ai_rec_page_refresh(void)
{
    if (s_screen == NULL) {
        return;
    }

    bsp_ai_rec_state_t state = bsp_ai_record_state();
    bool active = (state == BSP_AI_REC_RECORDING || state == BSP_AI_REC_PAUSED);
    uint32_t seconds = bsp_ai_record_seconds();

    const char *text = "就绪";
    if (state == BSP_AI_REC_RECORDING) {
        text = "录音中";
    } else if (state == BSP_AI_REC_PAUSED) {
        text = "已暂停";
    } else if (state == BSP_AI_REC_DONE) {
        text = "已保存";
    }
    lv_label_set_text(s_state_label, text);

    char buf[16];
    snprintf(buf, sizeof(buf), "%02u:%02u", (unsigned)(seconds / 60), (unsigned)(seconds % 60));
    lv_label_set_text(s_time_label, buf);

    set_disabled(s_start_btn, active);
    set_disabled(s_stop_btn, !active);
    set_disabled(s_finish_btn, !active);
    set_disabled(s_cancel_btn, !active);
    lv_label_set_text(s_stop_label, state == BSP_AI_REC_PAUSED ? "继续录音" : "停止录音");
}
