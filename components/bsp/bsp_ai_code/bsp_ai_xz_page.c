// bsp_ai_xz_page.c — 小智 AI 页面：激活码、状态、对话文本、说话按钮
#include "bsp_ai_xz.h"

#include <stdio.h>
#include <string.h>

#include "bsp_ai_font_cjk_16.h"
#include "bsp_ai_ui.h"
#include "esp_log.h"
#include "lvgl.h"

#define COLOR_BG      lv_color_hex(0x0E1116)
#define COLOR_CARD    lv_color_hex(0x1A2129)
#define COLOR_TEXT    lv_color_hex(0xF2F5F7)
#define COLOR_MUTED   lv_color_hex(0x8A97A6)
#define COLOR_ACCENT  lv_color_hex(0x1ED760)
#define COLOR_BTN_OFF lv_color_hex(0x39424E)

static lv_obj_t *s_screen;
static lv_obj_t *s_status;
static lv_obj_t *s_code;
static lv_obj_t *s_hint;
static lv_obj_t *s_reply;
static lv_timer_t *s_timer;
static char s_last_status[64];
static char s_last_code[16];
static char s_last_reply_text[512];
static int s_last_state = -1;

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, int x, int y, int w, int h,
                            lv_color_t color, const lv_font_t *font)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, w, h);
    lv_label_set_long_mode(label, LV_LABEL_LONG_WRAP);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, color, 0);
    lv_label_set_text(label, text);
    return label;
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_xz_set_active(false);
    bsp_ai_xz_stop();
    bsp_ai_show_main();
}

static const char *hint_for_state(int state)
{
    switch (state) {
    case BSP_AI_XZ_BINDING:
        return "在 xiaozhi.me 输入激活码";
    case BSP_AI_XZ_READY:
        return "直接说话即可，停顿后自动发送";
    case BSP_AI_XZ_LISTENING:
        return "请说话，停顿后自动发送";
    case BSP_AI_XZ_THINKING:
        return "云端思考中...";
    case BSP_AI_XZ_SPEAKING:
        return "正在播放回复";
    case BSP_AI_XZ_ERROR:
        return "请返回重试或检查 WiFi";
    default:
        return "正在连接小智云端...";
    }
}

static void timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bsp_ai_xz_snapshot_t snap;
    bsp_ai_xz_snapshot(&snap);

    if (strcmp(snap.status, s_last_status) != 0) {
        lv_label_set_text(s_status, snap.status);
        snprintf(s_last_status, sizeof(s_last_status), "%s", snap.status);
    }
    if (strcmp(snap.code, s_last_code) != 0) {
        lv_label_set_text(s_code, snap.code);
        snprintf(s_last_code, sizeof(s_last_code), "%s", snap.code);
    }

    char text[512];
    if (snap.user[0] != '\0' || snap.reply[0] != '\0') {
        snprintf(text, sizeof(text), "你：%s\n%s", snap.user, snap.reply);
    } else {
        snprintf(text, sizeof(text), "你好，我是小智。");
    }
    if (strcmp(text, s_last_reply_text) != 0) {
        lv_label_set_text(s_reply, text);
        snprintf(s_last_reply_text, sizeof(s_last_reply_text), "%s", text);
    }

    if (snap.state != s_last_state) {
        lv_label_set_text(s_hint, hint_for_state(snap.state));
        s_last_state = snap.state;
    }
}

static void screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (s_timer == NULL) {
        return;
    }
    if (code == LV_EVENT_SCREEN_LOADED) {
        bsp_ai_xz_set_active(true);
        lv_timer_resume(s_timer);
    } else if (code == LV_EVENT_SCREEN_UNLOADED) {
        bsp_ai_xz_set_active(false);
        lv_timer_pause(s_timer);
    }
}

static void build_ui(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, COLOR_BG, 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);
    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(s_screen, screen_event_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    lv_obj_t *back = lv_button_create(s_screen);
    lv_obj_set_pos(back, 6, 6);
    lv_obj_set_size(back, 74, 30);
    lv_obj_set_style_bg_color(back, COLOR_CARD, 0);
    lv_obj_set_style_radius(back, 8, 0);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_t *back_label = make_label(back, "返回", 0, 0, 74, 30, COLOR_TEXT, &bsp_ai_font_cjk_16);
    lv_obj_set_style_text_align(back_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_center(back_label);

    make_label(s_screen, "小智 AI", 92, 10, 220, 24, COLOR_TEXT, &bsp_ai_font_cjk_16);

    lv_obj_t *card = lv_obj_create(s_screen);
    lv_obj_set_pos(card, 8, 42);
    lv_obj_set_size(card, 304, 86);
    lv_obj_set_style_bg_color(card, COLOR_CARD, 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_set_style_border_width(card, 0, 0);
    lv_obj_set_style_pad_all(card, 0, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);

    s_status = make_label(card, "等待 WiFi", 10, 8, 284, 20, COLOR_MUTED, &bsp_ai_font_cjk_16);
    s_code = make_label(card, "------", 10, 30, 284, 30, COLOR_ACCENT, &lv_font_montserrat_24);
    lv_obj_set_style_text_align(s_code, LV_TEXT_ALIGN_CENTER, 0);
    s_hint = make_label(card, "正在连接小智云端...", 10, 62, 284, 18, COLOR_TEXT, &bsp_ai_font_cjk_16);

    s_reply = make_label(s_screen, "你好，我是小智。", 12, 134, 296, 72, COLOR_TEXT, &bsp_ai_font_cjk_16);

    lv_obj_t *foot = make_label(s_screen, "连续对话已开启，无需按键", 12, 210, 296, 20, COLOR_MUTED,
                                &bsp_ai_font_cjk_16);
    lv_obj_set_style_text_align(foot, LV_TEXT_ALIGN_CENTER, 0);

    s_timer = lv_timer_create(timer_cb, 200, NULL);
    timer_cb(s_timer);
}

lv_obj_t *bsp_ai_xz_page_create(void)
{
    if (s_screen == NULL) {
        build_ui();
    }
    return s_screen;
}

void bsp_ai_xz_page_refresh(void)
{
    if (s_timer != NULL) {
        timer_cb(s_timer);
    }
}
