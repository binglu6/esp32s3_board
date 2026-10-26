// bsp_ai_music.c — 音乐播放页面（返回 / 列表 / 上一首 / 播放暂停 / 下一首 / 音量）
#include "bsp_ai_music.h"

#include <stdio.h>

#include "bsp_ai_audio.h"
#include "bsp_ai_font_cjk_16.h"
#include "bsp_ai_sdcard.h"
#include "bsp_ai_ui.h"
#include "esp_log.h"

static const char *TAG = "bsp_ai_music";

#define COLOR_BG        (0x0E1116)
#define COLOR_CARD      (0x1A2129)
#define COLOR_CARD_DOWN (0x2A3542)
#define COLOR_TEXT      (0xF2F5F7)
#define COLOR_DIM       (0x8A97A5)
#define COLOR_ACCENT    (0x1ED760)
#define COLOR_ACCENT_DN (0x17B34E)

#define REFRESH_PERIOD_MS (200)

static lv_obj_t *s_screen = NULL;
static lv_obj_t *s_song_label = NULL;
static lv_obj_t *s_state_label = NULL;
static lv_obj_t *s_play_icon = NULL;
static lv_obj_t *s_volume_slider = NULL;
static lv_obj_t *s_volume_label = NULL;
static lv_timer_t *s_timer = NULL;
static size_t s_last_index = (size_t)-1;

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

static void back_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_main();
}

static void list_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_sd_dir(BSP_AI_MUSIC_DIR);
}

static void prev_click_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_ai_audio_prev() != ESP_OK) {
        ESP_LOGW(TAG, "previous track failed");
    }
    bsp_ai_music_page_refresh();
}

static void next_click_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_ai_audio_next() != ESP_OK) {
        ESP_LOGW(TAG, "next track failed");
    }
    bsp_ai_music_page_refresh();
}

static void play_click_cb(lv_event_t *e)
{
    (void)e;
    if (bsp_ai_audio_toggle() != ESP_OK) {
        ESP_LOGW(TAG, "toggle playback failed");
    }
    bsp_ai_music_page_refresh();
}

static void volume_changed_cb(lv_event_t *e)
{
    lv_obj_t *slider = lv_event_get_target(e);
    bsp_ai_audio_set_volume((int)lv_slider_get_value(slider));
    bsp_ai_music_page_refresh();
}

static void create_top_bar(lv_obj_t *parent)
{
    lv_obj_t *back = make_card_button(parent, 6, 6, 74, 30);
    make_label(back, "返回", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(back, 0));
    lv_obj_add_event_cb(back, back_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *title = make_label(parent, "音乐播放", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_align(title, LV_ALIGN_TOP_MID, 0, 12);

    lv_obj_t *list = make_card_button(parent, 240, 6, 74, 30);
    make_label(list, "列表", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_center(lv_obj_get_child(list, 0));
    lv_obj_add_event_cb(list, list_click_cb, LV_EVENT_CLICKED, NULL);
}

static void create_album(lv_obj_t *parent)
{
    lv_obj_t *album = lv_obj_create(parent);
    lv_obj_remove_style_all(album);
    lv_obj_set_size(album, 60, 60);
    lv_obj_align(album, LV_ALIGN_TOP_MID, 0, 42);
    lv_obj_set_style_radius(album, LV_RADIUS_CIRCLE, 0);
    lv_obj_set_style_bg_color(album, lv_color_hex(COLOR_CARD), 0);
    lv_obj_set_style_bg_opa(album, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(album, 2, 0);
    lv_obj_set_style_border_color(album, lv_color_hex(COLOR_ACCENT), 0);

    lv_obj_t *icon = make_label(album, LV_SYMBOL_AUDIO, &lv_font_montserrat_24, COLOR_ACCENT);
    lv_obj_center(icon);
}

static void create_song_info(lv_obj_t *parent)
{
    s_song_label = make_label(parent, "未选择音乐", &bsp_ai_font_cjk_16, COLOR_TEXT);
    lv_obj_set_width(s_song_label, 300);
    lv_obj_set_pos(s_song_label, 10, 108);
    lv_obj_set_style_text_align(s_song_label, LV_TEXT_ALIGN_CENTER, 0);
    lv_label_set_long_mode(s_song_label, LV_LABEL_LONG_SCROLL_CIRCULAR);

    s_state_label = make_label(parent, "按播放开始", &bsp_ai_font_cjk_16, COLOR_DIM);
    lv_obj_set_width(s_state_label, 300);
    lv_obj_set_pos(s_state_label, 10, 130);
    lv_obj_set_style_text_align(s_state_label, LV_TEXT_ALIGN_CENTER, 0);
}

static void create_controls(lv_obj_t *parent)
{
    lv_obj_t *prev = make_card_button(parent, 56, 154, 58, 46);
    lv_obj_t *prev_icon = make_label(prev, LV_SYMBOL_PREV, &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_center(prev_icon);
    lv_obj_add_event_cb(prev, prev_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *play = make_card_button(parent, 122, 154, 76, 46);
    lv_obj_set_style_bg_color(play, lv_color_hex(COLOR_ACCENT), 0);
    lv_obj_set_style_bg_color(play, lv_color_hex(COLOR_ACCENT_DN), LV_STATE_PRESSED);
    s_play_icon = make_label(play, LV_SYMBOL_PLAY, &lv_font_montserrat_24, COLOR_BG);
    lv_obj_center(s_play_icon);
    lv_obj_add_event_cb(play, play_click_cb, LV_EVENT_CLICKED, NULL);

    lv_obj_t *next = make_card_button(parent, 206, 154, 58, 46);
    lv_obj_t *next_icon = make_label(next, LV_SYMBOL_NEXT, &lv_font_montserrat_24, COLOR_TEXT);
    lv_obj_center(next_icon);
    lv_obj_add_event_cb(next, next_click_cb, LV_EVENT_CLICKED, NULL);
}

static void create_volume(lv_obj_t *parent)
{
    lv_obj_t *label = make_label(parent, "VOL", &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_pos(label, 12, 214);

    s_volume_slider = lv_slider_create(parent);
    lv_obj_set_pos(s_volume_slider, 52, 214);
    lv_obj_set_size(s_volume_slider, 200, 10);
    lv_slider_set_range(s_volume_slider, 0, 100);
    lv_slider_set_value(s_volume_slider, bsp_ai_audio_volume(), LV_ANIM_OFF);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_hex(COLOR_CARD_DOWN), LV_PART_MAIN);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_hex(COLOR_ACCENT), LV_PART_INDICATOR);
    lv_obj_set_style_bg_color(s_volume_slider, lv_color_hex(COLOR_ACCENT), LV_PART_KNOB);
    lv_obj_set_style_pad_all(s_volume_slider, 0, LV_PART_KNOB);
    lv_obj_add_event_cb(s_volume_slider, volume_changed_cb, LV_EVENT_VALUE_CHANGED, NULL);

    s_volume_label = make_label(parent, "60", &lv_font_montserrat_14, COLOR_DIM);
    lv_obj_set_width(s_volume_label, 40);
    lv_obj_set_pos(s_volume_label, 262, 214);
    lv_obj_set_style_text_align(s_volume_label, LV_TEXT_ALIGN_CENTER, 0);
}

static void music_timer_cb(lv_timer_t *timer)
{
    (void)timer;
    bsp_ai_music_page_refresh();
}

static void music_screen_event_cb(lv_event_t *e)
{
    if (lv_event_get_code(e) == LV_EVENT_SCREEN_LOADED) {
        lv_timer_resume(s_timer);
        bsp_ai_music_page_refresh();
    } else if (lv_event_get_code(e) == LV_EVENT_SCREEN_UNLOADED) {
        lv_timer_pause(s_timer);
    }
}

lv_obj_t *bsp_ai_music_page_create(void)
{
    if (s_screen != NULL) {
        return s_screen;
    }

    s_screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(s_screen, lv_color_hex(COLOR_BG), 0);
    lv_obj_set_style_bg_opa(s_screen, LV_OPA_COVER, 0);

    create_top_bar(s_screen);
    create_album(s_screen);
    create_song_info(s_screen);
    create_controls(s_screen);
    create_volume(s_screen);

    s_timer = lv_timer_create(music_timer_cb, REFRESH_PERIOD_MS, NULL);
    if (s_timer == NULL) {
        ESP_LOGE(TAG, "create refresh timer failed");
        return NULL;
    }
    lv_timer_pause(s_timer);

    lv_obj_add_event_cb(s_screen, music_screen_event_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(s_screen, music_screen_event_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    return s_screen;
}

void bsp_ai_music_page_refresh(void)
{
    if (s_screen == NULL) {
        return;
    }

    size_t count = bsp_ai_audio_count();
    size_t index = bsp_ai_audio_current();
    bool playing = bsp_ai_audio_is_playing();

    if (count == 0) {
        lv_label_set_text(s_song_label, "未找到音乐文件");
        lv_label_set_text(s_state_label, "请插入 SD 卡");
    } else {
        if (index != s_last_index) {
            char buf[160];
            snprintf(buf, sizeof(buf), "%02u. %s", (unsigned)(index + 1),
                     bsp_ai_audio_name(index));
            lv_label_set_text(s_song_label, buf);
            s_last_index = index;
        }
        lv_label_set_text(s_state_label, playing ? "正在播放"
                                                 : (bsp_ai_audio_started() ? "已暂停"
                                                                           : "按播放开始"));
    }

    lv_label_set_text(s_play_icon, playing ? LV_SYMBOL_PAUSE : LV_SYMBOL_PLAY);

    int volume = bsp_ai_audio_volume();
    if ((int)lv_slider_get_value(s_volume_slider) != volume) {
        lv_slider_set_value(s_volume_slider, volume, LV_ANIM_OFF);
    }
    char vol_text[8];
    snprintf(vol_text, sizeof(vol_text), "%d", volume);
    lv_label_set_text(s_volume_label, vol_text);
}
