// bsp_ai_dialog.c — 通用确认对话框
#include "bsp_ai_dialog.h"

#include "bsp_ai_font_cjk_16.h"
#include "esp_log.h"

static const char *TAG = "bsp_ai_dialog";

static lv_obj_t *s_box = NULL;
static bsp_ai_dialog_confirm_cb_t s_confirm_cb = NULL;
static void *s_confirm_ctx = NULL;

static void close_box(void)
{
    if (s_box != NULL) {
        lv_msgbox_close(s_box);
        s_box = NULL;
    }
    s_confirm_cb = NULL;
    s_confirm_ctx = NULL;
}

static void yes_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_dialog_confirm_cb_t cb = s_confirm_cb;
    void *ctx = s_confirm_ctx;
    close_box();
    if (cb != NULL) {
        cb(ctx);
    }
}

static void no_click_cb(lv_event_t *e)
{
    (void)e;
    close_box();
}

void bsp_ai_dialog_confirm(const char *title, const char *text, const char *yes,
                           bsp_ai_dialog_confirm_cb_t cb, void *ctx)
{
    close_box();

    s_confirm_cb = cb;
    s_confirm_ctx = ctx;

    s_box = lv_msgbox_create(NULL);
    if (s_box == NULL) {
        ESP_LOGE(TAG, "create msgbox failed");
        return;
    }
    lv_obj_set_width(s_box, 280);
    lv_obj_set_style_text_font(s_box, &bsp_ai_font_cjk_16, 0);

    lv_obj_t *title_label = lv_msgbox_add_title(s_box, title);
    lv_obj_t *text_label = lv_msgbox_add_text(s_box, text);
    if (title_label != NULL) {
        lv_obj_set_style_text_font(title_label, &bsp_ai_font_cjk_16, 0);
    }
    if (text_label != NULL) {
        lv_obj_set_style_text_font(text_label, &bsp_ai_font_cjk_16, 0);
    }

    lv_obj_t *no = lv_msgbox_add_footer_button(s_box, "取消");
    lv_obj_t *ok = lv_msgbox_add_footer_button(s_box, yes);
    if (no == NULL || ok == NULL) {
        ESP_LOGE(TAG, "add msgbox button failed");
        close_box();
        return;
    }
    lv_obj_set_style_text_font(no, &bsp_ai_font_cjk_16, 0);
    lv_obj_set_style_text_font(ok, &bsp_ai_font_cjk_16, 0);
    lv_obj_add_event_cb(no, no_click_cb, LV_EVENT_CLICKED, NULL);
    lv_obj_add_event_cb(ok, yes_click_cb, LV_EVENT_CLICKED, NULL);
}
