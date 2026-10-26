// bsp_ai_wifi_page.c — WiFi 扫描列表与密码输入页面
#include "bsp_ai_wifi_page.h"

#include <stdint.h>
#include <stdio.h>
#include <string.h>

#include "bsp_ai_font_cjk_16.h"
#include "bsp_ai_ui.h"
#include "bsp_ai_wifi.h"
#include "esp_log.h"
#include "lvgl.h"

static const char *TAG = "bsp_ai_wifi_page";

#define COLOR_BG      0x0E1116
#define COLOR_CARD    0x1A2129
#define COLOR_ROW     0x232B35
#define COLOR_TEXT    0xE6EDF3
#define COLOR_MUTED   0x8A93A0
#define COLOR_ACCENT  0x1ED760
#define COLOR_AMBER   0xF59E0B

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_status = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_modal = NULL;
static lv_obj_t *s_password = NULL;
static lv_obj_t *s_keyboard = NULL;
static lv_timer_t *s_timer = NULL;
static char s_target_ssid[BSP_AI_WIFI_SSID_MAX + 1];
static bool s_need_rebuild = false;
static bool s_was_connected = false;

static void copy_str(char *dst, size_t dst_len, const char *src)
{
    if (dst_len == 0) {
        return;
    }
    if (src == NULL) {
        dst[0] = '\0';
        return;
    }
    size_t n = strnlen(src, dst_len - 1);
    memcpy(dst, src, n);
    dst[n] = '\0';
}

static lv_obj_t *make_label(lv_obj_t *parent, const char *text, const lv_font_t *font,
                            uint32_t color)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, font, 0);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    return label;
}

static lv_obj_t *make_card(lv_obj_t *parent, int x, int y, int w, int h)
{
    lv_obj_t *card = lv_obj_create(parent);
    lv_obj_remove_style_all(card);
    lv_obj_set_pos(card, x, y);
    lv_obj_set_size(card, w, h);
    lv_obj_set_style_bg_color(card, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, 10, 0);
    lv_obj_clear_flag(card, LV_OBJ_FLAG_SCROLLABLE);
    return card;
}

static lv_obj_t *make_button(lv_obj_t *parent, int x, int y, int w, int h, uint32_t color)
{
    lv_obj_t *btn = lv_button_create(parent);
    lv_obj_remove_style_all(btn);
    lv_obj_set_pos(btn, x, y);
    lv_obj_set_size(btn, w, h);
    lv_obj_set_style_bg_color(btn, lv_color_hex(color), 0);
    lv_obj_set_style_bg_color(btn, lv_color_hex(0x0B0E12), LV_STATE_PRESSED);
    lv_obj_set_style_bg_opa(btn, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(btn, 8, 0);
    return btn;
}

static void start_connect(const char *password)
{
    if (bsp_ai_wifi_connect_async(s_target_ssid, password) != ESP_OK) {
        if (s_status != NULL) {
            lv_label_set_text(s_status, "连接请求失败");
        }
        return;
    }
    if (s_status != NULL) {
        lv_label_set_text_fmt(s_status, "正在连接 %s...", s_target_ssid);
    }
}

static void close_dialog(void)
{
    if (s_modal != NULL) {
        lv_obj_delete_async(s_modal);
        s_modal = NULL;
        s_password = NULL;
        s_keyboard = NULL;
    }
}

static void cancel_click_cb(lv_event_t *e)
{
    (void)e;
    close_dialog();
}

static void keyboard_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);
    if (code == LV_EVENT_READY) {
        char pass[BSP_AI_WIFI_PASS_MAX + 1];
        copy_str(pass, sizeof(pass), lv_textarea_get_text(s_password));
        close_dialog();
        start_connect(pass);
    } else if (code == LV_EVENT_CANCEL) {
        close_dialog();
    }
}

static void open_password_dialog(void)
{
    if (s_modal != NULL) {
        return;
    }

    s_modal = lv_obj_create(lv_layer_top());
    lv_obj_remove_style_all(s_modal);
    lv_obj_set_size(s_modal, 320, 240);
    lv_obj_set_style_bg_color(s_modal, lv_color_hex(0x000000), 0);
    lv_obj_set_style_bg_opa(s_modal, LV_OPA_50, 0);
    lv_obj_clear_flag(s_modal, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_add_flag(s_modal, LV_OBJ_FLAG_CLICKABLE);

    lv_obj_t *card = make_card(s_modal, 10, 4, 300, 118);
    lv_obj_t *title = make_label(card, s_target_ssid, &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_label_set_long_mode(title, LV_LABEL_LONG_DOT);
    lv_obj_set_width(title, 210);
    lv_obj_set_pos(title, 8, 8);

    lv_obj_t *cancel = make_button(card, 232, 6, 60, 30, 0x3A4552);
    lv_obj_t *cancel_label = make_label(cancel, "取消", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_set_pos(cancel_label, 14, 6);
    lv_obj_add_event_cb(cancel, cancel_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *hint = make_label(card, "密码", &bsp_ai_font_cjk_16, COLOR_MUTED);
    lv_obj_set_pos(hint, 8, 34);

    s_password = lv_textarea_create(card);
    lv_obj_set_pos(s_password, 8, 58);
    lv_obj_set_size(s_password, 284, 40);
    lv_textarea_set_one_line(s_password, true);
    lv_textarea_set_password_mode(s_password, true);
    lv_textarea_set_max_length(s_password, BSP_AI_WIFI_PASS_MAX);
    lv_textarea_set_placeholder_text(s_password, "请输入 WiFi 密码");
    lv_obj_set_style_text_font(s_password, &bsp_ai_font_cjk_16, 0);
    lv_obj_add_event_cb(s_password, keyboard_event_cb, LV_EVENT_ALL, NULL);

    /* lv_keyboard 构造时默认 LV_ALIGN_BOTTOM_MID，若再用 set_pos 会叠加偏移，
       导致键盘被推到屏幕外；这里保留底部对齐并显式指定尺寸。 */
    s_keyboard = lv_keyboard_create(s_modal);
    lv_obj_set_size(s_keyboard, 320, 116);
    lv_obj_align(s_keyboard, LV_ALIGN_BOTTOM_MID, 0, 0);
    lv_keyboard_set_mode(s_keyboard, LV_KEYBOARD_MODE_TEXT_LOWER);
    lv_keyboard_set_textarea(s_keyboard, s_password);
}

static void ap_click_cb(lv_event_t *e)
{
    uintptr_t index = (uintptr_t)lv_event_get_user_data(e);
    if (index >= bsp_ai_wifi_ap_count() || bsp_ai_wifi_connecting()) {
        return;
    }
    const char *ssid = bsp_ai_wifi_ap_ssid(index);
    if (ssid == NULL) {
        return;
    }
    copy_str(s_target_ssid, sizeof(s_target_ssid), ssid);
    if (bsp_ai_wifi_ap_secure((size_t)index)) {
        open_password_dialog();
    } else {
        start_connect("");
    }
}

static void add_ap_row(lv_obj_t *parent, size_t index)
{
    lv_obj_t *row = make_card(parent, 0, (int)index * 40, 296, 36);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_ROW), 0);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_add_flag(row, LV_OBJ_FLAG_CLICKABLE);

    int rssi = bsp_ai_wifi_ap_rssi(index);
    uint32_t color = (rssi >= -60) ? COLOR_ACCENT : ((rssi >= -75) ? COLOR_AMBER : 0xE5484D);
    lv_obj_t *icon = make_label(row, LV_SYMBOL_WIFI, &lv_font_montserrat_16, color);
    lv_obj_set_pos(icon, 8, 9);

    lv_obj_t *ssid = make_label(row, bsp_ai_wifi_ap_ssid(index), &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_label_set_long_mode(ssid, LV_LABEL_LONG_DOT);
    lv_obj_set_width(ssid, 186);
    lv_obj_set_pos(ssid, 30, 8);

    char rssi_text[12];
    snprintf(rssi_text, sizeof(rssi_text), "%d", rssi);
    lv_obj_t *label = make_label(row, rssi_text, &lv_font_montserrat_14, COLOR_MUTED);
    lv_obj_set_width(label, 50);
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_RIGHT, 0);
    lv_obj_set_pos(label, 238, 9);

    lv_obj_add_event_cb(row, ap_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
}

static void rebuild_list(void)
{
    if (s_list == NULL) {
        return;
    }
    lv_obj_clean(s_list);

    size_t count = bsp_ai_wifi_ap_count();
    if (count == 0) {
        lv_obj_t *hint = make_label(s_list, "未发现 WiFi", &bsp_ai_font_cjk_16, COLOR_MUTED);
        lv_obj_set_pos(hint, 96, 50);
        return;
    }
    for (size_t i = 0; i < count; i++) {
        add_ap_row(s_list, i);
    }
}

static void update_status(void)
{
    if (s_status == NULL) {
        return;
    }
    if (bsp_ai_wifi_connecting()) {
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_AMBER), 0);
        lv_label_set_text_fmt(s_status, "正在连接 %s...", s_target_ssid);
    } else if (bsp_ai_wifi_connected()) {
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_ACCENT), 0);
        lv_label_set_text_fmt(s_status, "已连接 %s", bsp_ai_wifi_current_ssid());
    } else if (bsp_ai_wifi_scan_busy()) {
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_MUTED), 0);
        lv_label_set_text(s_status, "正在扫描附近 WiFi...");
    } else {
        lv_obj_set_style_text_color(s_status, lv_color_hex(COLOR_MUTED), 0);
        lv_label_set_text(s_status, "未连接，点击 WiFi 名称连接");
    }
}

static void refresh_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    update_status();

    bool connected = bsp_ai_wifi_connected();
    if (!bsp_ai_wifi_scan_busy() && bsp_ai_wifi_scan_done()) {
        if (s_need_rebuild || connected != s_was_connected) {
            s_need_rebuild = false;
            s_was_connected = connected;
            rebuild_list();
        }
    }
}

static void scan_click_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_ai_wifi_scan_busy()) {
        return;
    }
    if (bsp_ai_wifi_scan_start() != ESP_OK) {
        if (s_status != NULL) {
            lv_label_set_text(s_status, "扫描启动失败");
        }
        return;
    }
    s_need_rebuild = true;
    update_status();
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    close_dialog();
    bsp_ai_show_main();
}

static void create_top_bar(void)
{
    lv_obj_t *back = make_button(s_screen, 4, 4, 40, 40, COLOR_CARD);
    lv_obj_t *back_icon = make_label(back, LV_SYMBOL_LEFT, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_center(back_icon);
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = make_label(s_screen, "WiFi 连接", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_set_pos(title, 52, 12);

    lv_obj_t *scan = make_button(s_screen, 240, 6, 72, 36, COLOR_ACCENT);
    lv_obj_t *scan_text = make_label(scan, "扫描", &bsp_ai_font_cjk_16, 0x06170C);
    lv_obj_center(scan_text);
    lv_obj_add_event_cb(scan, scan_click_cb, LV_EVENT_CLICKED, NULL);

    s_status = make_label(s_screen, "未连接", &bsp_ai_font_cjk_16, COLOR_MUTED);
    lv_obj_set_pos(s_status, 12, 50);
    lv_obj_set_width(s_status, 296);
    lv_label_set_long_mode(s_status, LV_LABEL_LONG_DOT);
}

static void create_list(void)
{
    s_list = make_card(s_screen, 8, 74, 304, 158);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_add_flag(s_list, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
}

void bsp_ai_wifi_page_refresh(void)
{
    s_need_rebuild = true;
    s_was_connected = bsp_ai_wifi_connected();
    update_status();
    if (!bsp_ai_wifi_scan_busy() && bsp_ai_wifi_scan_start() != ESP_OK) {
        ESP_LOGW(TAG, "scan start failed");
    }
}

lv_obj_t *bsp_ai_wifi_page_create(void)
{
    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_clear_flag(s_screen, LV_OBJ_FLAG_SCROLLABLE);

    create_top_bar();
    create_list();

    s_timer = lv_timer_create(refresh_timer_cb, 400, NULL);
    bsp_ai_wifi_page_refresh();
    return s_screen;
}
