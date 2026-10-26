// bsp_ai_sd_page.c — SD 卡文件夹浏览器（music / 录音文件 / rgb led / WiFi）
#include "bsp_ai_sd_page.h"

#include <dirent.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>

#include "bsp_ai_audio.h"
#include "bsp_ai_dialog.h"
#include "bsp_ai_font_cjk_16.h"
#include "bsp_ai_sdcard.h"
#include "bsp_ai_ui.h"
#include "bsp_ai_wifi.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"

static const char *TAG = "bsp_ai_sd_page";

#define COLOR_BG        (0x0E1116)
#define COLOR_CARD      (0x1A2129)
#define COLOR_CARD_DOWN (0x2A3542)
#define COLOR_TEXT      (0xF2F5F7)
#define COLOR_DIM       (0x8A97A5)
#define COLOR_ACCENT    (0x1ED760)

#define PATH_LEN        (300)
#define NAME_LEN        (128)
#define MAX_ENTRIES     (24)

typedef enum {
    VIEW_ROOT = 0,
    VIEW_FILES,
    VIEW_INFO,
} sd_view_t;

typedef struct {
    char name[NAME_LEN];
} sd_entry_t;

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_list = NULL;
static lv_obj_t *s_title = NULL;

static sd_view_t s_view = VIEW_ROOT;
static char s_dir[PATH_LEN];
static char s_info_path[PATH_LEN];
static char s_info_parent[PATH_LEN];
static sd_entry_t s_entries[MAX_ENTRIES];
static size_t s_entry_count = 0;

static uint8_t s_info_red = 0;
static uint8_t s_info_green = 0;
static uint8_t s_info_blue = 0;
static uint8_t s_info_brightness = 0;
static bool s_info_ok = false;

static const char *const s_root_dirs[] = {
    BSP_AI_MUSIC_DIR,
    BSP_AI_REC_DIR,
    BSP_AI_RGB_DIR,
    BSP_AI_WIFI_DIR,
};
static const char *const s_root_names[] = {
    "music",
    "录音文件",
    "rgb led",
    "WiFi",
};

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

static const char *base_name(const char *path)
{
    const char *slash = strrchr(path, '/');
    return (slash != NULL) ? slash + 1 : path;
}

static void clear_list(void)
{
    uint32_t count = lv_obj_get_child_count(s_list);
    for (int32_t i = (int32_t)count - 1; i >= 0; i--) {
        lv_obj_delete(lv_obj_get_child(s_list, (uint32_t)i));
    }
    s_entry_count = 0;
}

static lv_obj_t *add_row(const char *icon, const char *text, uint32_t color, lv_event_cb_t cb,
                         uint32_t value, int text_w)
{
    lv_obj_t *row = lv_button_create(s_list);
    lv_obj_set_width(row, lv_pct(100));
    lv_obj_set_height(row, 38);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_color(row, lv_color_hex(COLOR_CARD_DOWN), LV_STATE_PRESSED);
    lv_obj_set_style_radius(row, 8, 0);
    lv_obj_set_style_shadow_width(row, 0, 0);
    lv_obj_set_style_border_width(row, 0, 0);
    lv_obj_set_style_pad_left(row, 10, 0);
    lv_obj_set_style_pad_right(row, 10, 0);
    lv_obj_add_event_cb(row, cb, LV_EVENT_CLICKED, (void *)(uintptr_t)value);

    lv_obj_t *icon_label = make_label(row, icon, &lv_font_montserrat_16, COLOR_ACCENT);
    lv_obj_align(icon_label, LV_ALIGN_LEFT_MID, 0, 0);

    lv_obj_t *label = make_label(row, text, &bsp_ai_font_cjk_16, color);
    lv_obj_set_width(label, text_w);
    lv_obj_set_pos(label, 26, 0);
    lv_obj_align(label, LV_ALIGN_LEFT_MID, 26, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_DOT);
    return row;
}

// ---------------- 根目录 ----------------

static void root_click_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= sizeof(s_root_dirs) / sizeof(s_root_dirs[0])) {
        return;
    }
    bsp_ai_sd_page_show_dir(s_root_dirs[index]);
}

static void refresh_root(void)
{
    lv_label_set_text(s_title, "SD 卡");
    clear_list();
    for (size_t i = 0; i < sizeof(s_root_dirs) / sizeof(s_root_dirs[0]); i++) {
        add_row(LV_SYMBOL_DIRECTORY, s_root_names[i], COLOR_TEXT, root_click_cb, (uint32_t)i, 250);
    }
}

// ---------------- 文件列表 ----------------

static bool audio_ext_ok(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (dot == NULL) {
        return false;
    }
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0;
}

static bool entry_wanted(const char *name)
{
    const char *dot = strrchr(name, '.');
    if (strcmp(s_dir, BSP_AI_MUSIC_DIR) == 0) {
        return audio_ext_ok(name);
    }
    if (strcmp(s_dir, BSP_AI_REC_DIR) == 0) {
        return dot != NULL && strcasecmp(dot, ".wav") == 0;
    }
    return dot != NULL && strcasecmp(dot, ".txt") == 0;
}

static bool is_regular_file(const char *dir, const char *name)
{
    char path[PATH_LEN];
    struct stat st;
    snprintf(path, sizeof(path), "%s/%s", dir, name);
    if (stat(path, &st) != 0) {
        return false;
    }
    return !S_ISDIR(st.st_mode);
}

static int entry_cmp(const void *a, const void *b)
{
    const sd_entry_t *ea = (const sd_entry_t *)a;
    const sd_entry_t *eb = (const sd_entry_t *)b;
    return strcasecmp(ea->name, eb->name);
}

static void scan_files(void)
{
    DIR *dp = opendir(s_dir);
    if (dp == NULL) {
        ESP_LOGW(TAG, "open dir %s failed", s_dir);
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dp)) != NULL && s_entry_count < MAX_ENTRIES) {
        if (ent->d_name[0] == '.' || !entry_wanted(ent->d_name)) {
            continue;
        }
        if (!is_regular_file(s_dir, ent->d_name)) {
            continue;
        }
        strncpy(s_entries[s_entry_count].name, ent->d_name, NAME_LEN - 1);
        s_entries[s_entry_count].name[NAME_LEN - 1] = '\0';
        s_entry_count++;
    }
    closedir(dp);
    qsort(s_entries, s_entry_count, sizeof(s_entries[0]), entry_cmp);
}

static void open_info(const char *path)
{
    strncpy(s_info_path, path, sizeof(s_info_path) - 1);
    s_info_path[sizeof(s_info_path) - 1] = '\0';
    snprintf(s_info_parent, sizeof(s_info_parent), "%s", s_dir);
    s_view = VIEW_INFO;
    bsp_ai_sd_page_refresh();
}

static bool is_rgb_dir(void)
{
    return strcmp(s_dir, BSP_AI_RGB_DIR) == 0;
}

static bool is_wifi_dir(void)
{
    return strcmp(s_dir, BSP_AI_WIFI_DIR) == 0;
}

static void file_click_cb(lv_event_t *e)
{
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= s_entry_count) {
        return;
    }

    char path[PATH_LEN + NAME_LEN + 2];
    snprintf(path, sizeof(path), "%s/%s", s_dir, s_entries[index].name);

    if (is_rgb_dir() || is_wifi_dir()) {
        open_info(path);
        return;
    }

    bsp_ai_audio_src_t src = (strcmp(s_dir, BSP_AI_REC_DIR) == 0) ? BSP_AI_AUDIO_SRC_RECORD
                                                                  : BSP_AI_AUDIO_SRC_MUSIC;
    if (bsp_ai_audio_select_source(src) != ESP_OK) {
        ESP_LOGW(TAG, "scan source failed");
    }
    if (bsp_ai_audio_play_path(path) != ESP_OK) {
        ESP_LOGW(TAG, "play %s failed", path);
        return;
    }

    if (src == BSP_AI_AUDIO_SRC_RECORD) {
        bsp_ai_show_rec_player();
    } else {
        bsp_ai_show_music();
    }
}

static char s_pending_delete[PATH_LEN + NAME_LEN + 2];

static void delete_yes_cb(void *ctx)
{
    (void)ctx;
    const char *current = bsp_ai_audio_path(bsp_ai_audio_current());
    if (current != NULL && strcmp(current, s_pending_delete) == 0) {
        (void)bsp_ai_audio_stop();
        vTaskDelay(pdMS_TO_TICKS(100));
    }
    if (remove(s_pending_delete) == 0) {
        ESP_LOGI(TAG, "removed %s", s_pending_delete);
    } else {
        ESP_LOGW(TAG, "remove %s failed", s_pending_delete);
    }
    (void)bsp_ai_audio_rescan();
    bsp_ai_sd_page_refresh();
}

static void trash_click_cb(lv_event_t *e)
{
    // 阻止事件冒泡到整行，避免删除时又触发播放
    lv_event_stop_bubbling(e);
    uint32_t index = (uint32_t)(uintptr_t)lv_event_get_user_data(e);
    if (index >= s_entry_count) {
        return;
    }
    snprintf(s_pending_delete, sizeof(s_pending_delete), "%s/%s", s_dir,
             s_entries[index].name);
    bsp_ai_dialog_confirm("删除录音", s_entries[index].name, "删除", delete_yes_cb, NULL);
}

static void attach_delete(lv_obj_t *row, uint32_t index)
{
    lv_obj_t *trash = lv_button_create(row);
    lv_obj_set_size(trash, 34, 30);
    lv_obj_align(trash, LV_ALIGN_RIGHT_MID, 0, 0);
    lv_obj_set_style_bg_color(trash, lv_color_hex(0xE5484D), 0);
    lv_obj_set_style_bg_color(trash, lv_color_hex(0x8C1D22), LV_STATE_PRESSED);
    lv_obj_set_style_radius(trash, 8, 0);
    lv_obj_set_style_shadow_width(trash, 0, 0);
    lv_obj_set_style_border_width(trash, 0, 0);
    lv_obj_t *icon = make_label(trash, LV_SYMBOL_TRASH, &lv_font_montserrat_16, COLOR_TEXT);
    lv_obj_center(icon);
    lv_obj_add_event_cb(trash, trash_click_cb, LV_EVENT_CLICKED, (void *)(uintptr_t)index);
}

static void add_empty_hint(const char *title, const char *tip)
{    lv_obj_t *label = make_label(s_list, title, &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_set_width(label, lv_pct(100));
    lv_obj_set_style_text_align(label, LV_TEXT_ALIGN_CENTER, 0);
    lv_obj_set_style_pad_top(label, 20, 0);

    lv_obj_t *sub = make_label(s_list, tip, &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_set_width(sub, lv_pct(100));
    lv_obj_set_style_text_align(sub, LV_TEXT_ALIGN_CENTER, 0);
}

static void refresh_files(void)
{
    lv_label_set_text(s_title, base_name(s_dir));
    clear_list();
    scan_files();

    if (s_entry_count == 0) {
        if (is_rgb_dir()) {
            add_empty_hint("未找到配置文件", "在 RGB 灯页面保存");
        } else if (strcmp(s_dir, BSP_AI_REC_DIR) == 0) {
            add_empty_hint("未找到录音文件", "请先录音");
        } else if (is_wifi_dir()) {
            add_empty_hint("暂无 WiFi 记录", "在 WiFi 页面连接后自动保存");
        } else {
            add_empty_hint("未找到音乐文件", "支持 mp3 / wav");
        }
        return;
    }

    bool is_rec = (strcmp(s_dir, BSP_AI_REC_DIR) == 0);
    const char *icon = is_rgb_dir() ? LV_SYMBOL_FILE
                                    : (is_wifi_dir() ? LV_SYMBOL_WIFI : LV_SYMBOL_AUDIO);
    for (size_t i = 0; i < s_entry_count; i++) {
        lv_obj_t *row = add_row(icon, s_entries[i].name, COLOR_TEXT, file_click_cb,
                                (uint32_t)i, is_rec ? 196 : 250);
        if (is_rec) {
            attach_delete(row, (uint32_t)i);
        }
    }
}

// ---------------- 颜色信息 ----------------

static void load_info(void)
{
    s_info_ok = false;
    s_info_red = 0;
    s_info_green = 0;
    s_info_blue = 0;
    s_info_brightness = 0;

    FILE *fp = fopen(s_info_path, "r");
    if (fp == NULL) {
        return;
    }
    char line[64];
    while (fgets(line, sizeof(line), fp) != NULL) {
        char *eq = strchr(line, '=');
        if (eq == NULL) {
            continue;
        }
        int value = atoi(eq + 1);
        if (strncmp(line, "BRIGHTNESS", 10) == 0) {
            s_info_brightness = (uint8_t)value;
        } else if (line[0] == 'R') {
            s_info_red = (uint8_t)value;
        } else if (line[0] == 'G') {
            s_info_green = (uint8_t)value;
        } else if (line[0] == 'B') {
            s_info_blue = (uint8_t)value;
        }
    }
    fclose(fp);
    s_info_ok = true;
}

static void refresh_wifi_info(void)
{
    lv_label_set_text(s_title, "WiFi 信息");

    char ssid[BSP_AI_WIFI_SSID_MAX + 1] = {0};
    char password[BSP_AI_WIFI_PASS_MAX + 1] = {0};
    if (bsp_ai_wifi_read_file(s_info_path, ssid, sizeof(ssid), password,
                              sizeof(password)) != ESP_OK) {
        add_empty_hint("无法读取 WiFi 记录", "");
        return;
    }

    lv_obj_t *name = make_label(s_list, base_name(s_info_path), &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    char buf[160];
    snprintf(buf, sizeof(buf), "SSID: %s", ssid);
    lv_obj_t *ssid_label = make_label(s_list, buf, &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_set_width(ssid_label, lv_pct(100));
    lv_label_set_long_mode(ssid_label, LV_LABEL_LONG_WRAP);

    snprintf(buf, sizeof(buf), "密码: %s", (password[0] != '\0') ? password : "(开放网络)");
    lv_obj_t *pass_label = make_label(s_list, buf, &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_set_width(pass_label, lv_pct(100));
    lv_label_set_long_mode(pass_label, LV_LABEL_LONG_WRAP);
}

static void refresh_rgb_info(void)
{
    lv_label_set_text(s_title, "颜色信息");
    load_info();

    lv_obj_t *name = make_label(s_list, base_name(s_info_path), &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_set_width(name, lv_pct(100));
    lv_obj_set_style_text_align(name, LV_TEXT_ALIGN_CENTER, 0);

    if (!s_info_ok) {
        add_empty_hint("无法读取配置文件", "");
        return;
    }

    lv_obj_t *swatch = lv_obj_create(s_list);
    lv_obj_remove_style_all(swatch);
    lv_obj_set_size(swatch, 80, 44);
    lv_obj_set_style_radius(swatch, 8, 0);
    lv_obj_set_style_bg_opa(swatch, LV_OPA_COVER, 0);
    uint8_t r = (uint8_t)(s_info_red * s_info_brightness / 100);
    uint8_t g = (uint8_t)(s_info_green * s_info_brightness / 100);
    uint8_t b = (uint8_t)(s_info_blue * s_info_brightness / 100);
    lv_obj_set_style_bg_color(swatch, lv_color_make(r, g, b), 0);

    char buf[64];
    snprintf(buf, sizeof(buf), "R = %u", s_info_red);
    make_label(s_list, buf, &bsp_ai_font_cjk_16, COLOR_TEXT);
    snprintf(buf, sizeof(buf), "G = %u", s_info_green);
    make_label(s_list, buf, &bsp_ai_font_cjk_16, COLOR_TEXT);
    snprintf(buf, sizeof(buf), "B = %u", s_info_blue);
    make_label(s_list, buf, &bsp_ai_font_cjk_16, COLOR_TEXT);
    snprintf(buf, sizeof(buf), "亮度 = %u%%", s_info_brightness);
    make_label(s_list, buf, &bsp_ai_font_cjk_16, COLOR_TEXT);
}

static void refresh_info(void)
{
    clear_list();
    if (is_wifi_dir()) {
        refresh_wifi_info();
    } else {
        refresh_rgb_info();
    }
}

// ---------------- 页面 ----------------

void bsp_ai_sd_page_refresh(void)
{
    if (s_screen == NULL) {
        return;
    }
    if (s_view == VIEW_ROOT) {
        refresh_root();
    } else if (s_view == VIEW_FILES) {
        refresh_files();
    } else {
        refresh_info();
    }
}

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    if (s_view == VIEW_ROOT) {
        bsp_ai_show_main();
    } else if (s_view == VIEW_INFO) {
        bsp_ai_sd_page_show_dir(s_info_parent);
    } else {
        bsp_ai_sd_page_show_dir(NULL);
    }
}

static void refresh_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_sd_page_refresh();
}

static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *back = make_card_button(parent, 6, 6, 74, 30);
    make_label(back, "返回", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(back, 0));
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);

    s_title = make_label(parent, "SD 卡", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_align(s_title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *refresh = make_card_button(parent, 240, 6, 74, 30);
    make_label(refresh, "刷新", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(refresh, 0));
    lv_obj_add_event_cb(refresh, refresh_click_cb, LV_EVENT_CLICKED, NULL);
}

lv_obj_t *bsp_ai_sd_page_create(void)
{
    if (s_screen != NULL) {
        return s_screen;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    create_top_bar(s_screen);

    s_list = lv_obj_create(s_screen);
    lv_obj_set_pos(s_list, 8, 44);
    lv_obj_set_size(s_list, 304, 188);
    lv_obj_set_style_bg_opa(s_list, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_list, 0, 0);
    lv_obj_set_style_pad_all(s_list, 4, 0);
    lv_obj_set_style_pad_row(s_list, 6, 0);
    lv_obj_set_flex_flow(s_list, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_scroll_dir(s_list, LV_DIR_VER);
    return s_screen;
}

void bsp_ai_sd_page_show_dir(const char *dir)
{
    if (dir == NULL) {
        s_view = VIEW_ROOT;
        s_dir[0] = '\0';
    } else {
        s_view = VIEW_FILES;
        snprintf(s_dir, sizeof(s_dir), "%s", dir);
    }
    bsp_ai_sd_page_refresh();
}
