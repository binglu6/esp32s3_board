#include "bsp_ai_code.h"

#include <string.h>
#include "logprint.h"
#include "bsp_ai_audio.h"
#include "bsp_ai_music.h"
#include "bsp_ai_rec_page.h"
#include "bsp_ai_rec_player.h"
#include "bsp_ai_rgb.h"
#include "bsp_ai_rgb_page.h"
#include "bsp_ai_sd_page.h"
#include "bsp_ai_ui.h"
#include "bsp_ai_wifi.h"
#include "bsp_ai_wifi_page.h"
#include "bsp_ai_xz.h"
#include "bsp_ili9341.h"
#include "bsp_gc0308.h"
#include "bsp_lvgl_display.h"
#include "esp_camera.h"
#include "esp_lvgl_port.h"
#include "lvgl.h"
#include "ai_btn_img.h"
#include "camera_btn_img.h"
#include "music_btn_img.h"
#include "rec_btn_img.h"
#include "rgb_btn_img.h"
#include "sd_btn_img.h"
#include "wifi_btn_img.h"

static const char *TAG = "bsp_ai_code";

// 摄像头输出分辨率（与 FRAMESIZE_QVGA 一致）
#define CAM_FRAME_W             (320)
#define CAM_FRAME_H             (240)
// 抓帧刷新周期（ms），约 30fps
#define CAM_REFRESH_PERIOD_MS   (33)

// 主页面按钮尺寸（预渲染位图）
#define MAIN_BTN_W              (100)
#define MAIN_BTN_H              (64)

// 持久化 LVGL 图像缓冲（双缓冲）：摄像头帧拷贝到这里，避免与 camera_fb 生命周期耦合；
// 双缓冲可避免 LVGL 分块渲染期间源缓冲被下一帧覆盖导致的画面撕裂
static uint16_t *s_cam_buf[2] = {NULL, NULL};
static lv_image_dsc_t s_cam_img_dsc[2];
static lv_obj_t *s_cam_img = NULL;
static lv_timer_t *s_cam_timer = NULL;
static int s_active = 0; // 当前正在渲染的缓冲索引，仅在 LVGL 任务内访问

// 页面：主页面、摄像头、音乐、录音、录音播放、SD 卡、RGB 灯、WiFi、AI 助手
static lv_obj_t *s_main_screen = NULL;
static lv_obj_t *s_camera_screen = NULL;
static lv_obj_t *s_music_screen = NULL;
static lv_obj_t *s_rec_screen = NULL;
static lv_obj_t *s_rec_player_screen = NULL;
static lv_obj_t *s_sd_screen = NULL;
static lv_obj_t *s_rgb_screen = NULL;
static lv_obj_t *s_wifi_screen = NULL;
static lv_obj_t *s_ai_screen = NULL;

/**
 * 把摄像头 RGB565 帧（大端，高字节在前）转换为 LVGL RGB565（小端，低字节在前）。
 * 本项目 esp_lvgl_port 配置 swap_bytes=true，即 LVGL 内部缓冲为小端，刷屏前由 port 交换字节。
 */
static void cam_fb_to_lvgl_rgb565(const camera_fb_t *fb, uint16_t *dst)
{
    const uint8_t *src = (const uint8_t *)fb->buf;
    size_t pixels = fb->len / 2;

    for (size_t i = 0; i < pixels; i++) {
        uint8_t hi = src[i * 2];     // 摄像头高字节（R 与 G 高 3 位）
        uint8_t lo = src[i * 2 + 1]; // 摄像头低字节（G 低 3 位与 B）
        dst[i] = (uint16_t)((hi << 8) | lo); // 小端写入，得到 [lo, hi]
    }
}

// LVGL 定时器回调：抓取一帧摄像头图像并刷新（运行在 LVGL 任务内，锁已持有）
static void cam_lvgl_timer_cb(lv_timer_t *timer)
{
    (void)timer;

    camera_fb_t *fb = esp_camera_fb_get();
    if (fb == NULL) {
        ESP_LOGW(TAG, "get camera frame failed");
        return;
    }

    if (fb->width != CAM_FRAME_W || fb->height != CAM_FRAME_H || fb->format != PIXFORMAT_RGB565) {
        ESP_LOGW(TAG, "unexpected frame %dx%d fmt=%d", fb->width, fb->height, fb->format);
        esp_camera_fb_return(fb);
        return;
    }

    // 写入空闲缓冲（非当前渲染中的那块），避免撕裂
    int idx = s_active ^ 1;
    cam_fb_to_lvgl_rgb565(fb, s_cam_buf[idx]);
    esp_camera_fb_return(fb);

    // 切换到新缓冲并通知 LVGL 重绘
    s_active = idx;
    lv_image_set_src(s_cam_img, &s_cam_img_dsc[idx]);
    lv_obj_invalidate(s_cam_img);
}

// ---------------- 页面切换 ----------------

void bsp_ai_show_main(void)
{
    lv_screen_load(s_main_screen);
}

void bsp_ai_show_camera(void)
{
    lv_screen_load(s_camera_screen);
}

void bsp_ai_show_music(void)
{
    if (s_music_screen == NULL) {
        s_music_screen = bsp_ai_music_page_create();
    }
    if (s_music_screen == NULL) {
        ESP_LOGE(TAG, "create music page failed");
        return;
    }
    if (bsp_ai_audio_select_source(BSP_AI_AUDIO_SRC_MUSIC) != ESP_OK) {
        ESP_LOGW(TAG, "no music file found");
    }
    lv_screen_load(s_music_screen);
    bsp_ai_music_page_refresh();
}

void bsp_ai_show_rec(void)
{
    if (s_rec_screen == NULL) {
        s_rec_screen = bsp_ai_rec_page_create();
    }
    if (s_rec_screen == NULL) {
        ESP_LOGE(TAG, "create record page failed");
        return;
    }
    lv_screen_load(s_rec_screen);
    bsp_ai_rec_page_refresh();
}

void bsp_ai_show_rec_player(void)
{
    if (s_rec_player_screen == NULL) {
        s_rec_player_screen = bsp_ai_rec_player_create();
    }
    if (s_rec_player_screen == NULL) {
        ESP_LOGE(TAG, "create record player failed");
        return;
    }
    if (bsp_ai_audio_select_source(BSP_AI_AUDIO_SRC_RECORD) != ESP_OK) {
        ESP_LOGW(TAG, "no recording file found");
    }
    lv_screen_load(s_rec_player_screen);
    bsp_ai_rec_player_refresh();
}

void bsp_ai_show_rgb(void)
{
    if (s_rgb_screen == NULL) {
        s_rgb_screen = bsp_ai_rgb_page_create();
    }
    if (s_rgb_screen == NULL) {
        ESP_LOGE(TAG, "create rgb page failed");
        return;
    }
    lv_screen_load(s_rgb_screen);
    bsp_ai_rgb_page_sync();
}

void bsp_ai_show_sd(void)
{
    bsp_ai_show_sd_dir(NULL);
}

void bsp_ai_show_sd_dir(const char *dir)
{
    if (s_sd_screen == NULL) {
        s_sd_screen = bsp_ai_sd_page_create();
    }
    if (s_sd_screen == NULL) {
        ESP_LOGE(TAG, "create sd page failed");
        return;
    }
    lv_screen_load(s_sd_screen);
    bsp_ai_sd_page_show_dir(dir);
}

void bsp_ai_show_wifi(void)
{
    if (s_wifi_screen == NULL) {
        s_wifi_screen = bsp_ai_wifi_page_create();
    }
    if (s_wifi_screen == NULL) {
        ESP_LOGE(TAG, "create wifi page failed");
        return;
    }
    lv_screen_load(s_wifi_screen);
    bsp_ai_wifi_page_refresh();
}

void bsp_ai_show_ai(void)
{
    if (s_ai_screen == NULL) {
        s_ai_screen = bsp_ai_xz_page_create();
    }
    if (s_ai_screen == NULL) {
        ESP_LOGE(TAG, "create ai page failed");
        return;
    }
    lv_screen_load(s_ai_screen);
    bsp_ai_xz_page_refresh();
}

// ---------------- 主页面 ----------------

static void main_music_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_music();
}

static void main_rec_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_rec();
}

static void main_sd_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_sd();
}

static void main_rgb_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_rgb();
}

static void main_wifi_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_wifi();
}

static void main_ai_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_ai();
}

static void main_camera_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_camera();
}

// 主页面功能按钮：统一 100x64 的预渲染 RGB565 位图（内置字库缺中文，按钮文字
// 用 .claude/scripts/gen_main_btns.py 提前绘制成位图）。
static void add_main_button(lv_obj_t *parent, const lv_image_dsc_t *img, int x, int y,
                            lv_event_cb_t cb)
{
    lv_obj_t *btn = lv_image_create(parent);
    lv_image_set_src(btn, img);
    lv_obj_set_pos(btn, x, y);
    lv_obj_add_flag(btn, LV_OBJ_FLAG_CLICKABLE);
    // 按下时整体变暗，提供按压反馈
    lv_obj_set_style_opa(btn, LV_OPA_70, LV_STATE_PRESSED);
    lv_obj_add_event_cb(btn, cb, LV_EVENT_CLICKED, NULL);
}

// 创建主页面：音乐、录音、SD 卡、RGB 灯、AI 助手、WiFi、摄像头七个功能按钮
static lv_obj_t *bsp_ai_create_main_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0xF2F5F7), 0);

    add_main_button(screen, &music_btn_img_dsc, 6, 6, main_music_cb);
    add_main_button(screen, &rec_btn_img_dsc, 110, 6, main_rec_cb);
    add_main_button(screen, &sd_btn_img_dsc, 214, 6, main_sd_cb);
    add_main_button(screen, &rgb_btn_img_dsc, 6, 80, main_rgb_cb);
    add_main_button(screen, &ai_btn_img_dsc, 110, 80, main_ai_cb);
    add_main_button(screen, &wifi_btn_img_dsc, 214, 80, main_wifi_cb);
    add_main_button(screen, &camera_btn_img_dsc, 110, 154, main_camera_cb);

    return screen;
}

// ---------------- 摄像头页面 ----------------

// 摄像头页面点击：返回主页面
static void camera_screen_click_cb(lv_event_t *e)
{
    (void)e;
    bsp_ai_show_main();
}

// 摄像头页面加载/卸载时，启停抓帧定时器，避免非显示期间白白抓帧
static void camera_screen_event_cb(lv_event_t *e)
{
    lv_event_code_t code = lv_event_get_code(e);

    if (code == LV_EVENT_SCREEN_LOADED) {
        lv_timer_resume(s_cam_timer);
    } else if (code == LV_EVENT_SCREEN_UNLOADED) {
        lv_timer_pause(s_cam_timer);
    }
}

// 创建摄像头页面：全屏图像控件，点击屏幕返回主页面
static lv_obj_t *bsp_ai_create_camera_screen(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);

    s_cam_img = lv_image_create(screen);
    lv_image_set_src(s_cam_img, &s_cam_img_dsc[0]);
    lv_obj_set_pos(s_cam_img, 0, 0);
    lv_obj_set_size(s_cam_img, CAM_FRAME_W, CAM_FRAME_H);
    lv_obj_add_flag(s_cam_img, LV_OBJ_FLAG_CLICKABLE);
    lv_obj_add_event_cb(s_cam_img, camera_screen_click_cb, LV_EVENT_CLICKED, NULL);

    // 页面加载/卸载时切换抓帧定时器的运行状态
    lv_obj_add_event_cb(screen, camera_screen_event_cb, LV_EVENT_SCREEN_LOADED, NULL);
    lv_obj_add_event_cb(screen, camera_screen_event_cb, LV_EVENT_SCREEN_UNLOADED, NULL);

    return screen;
}

// 分配摄像头 RGB565 双缓冲并初始化 LVGL 图像描述符
static esp_err_t bsp_ai_alloc_camera_buffers(void)
{
    size_t buf_size = CAM_FRAME_W * CAM_FRAME_H * sizeof(uint16_t);

    for (int i = 0; i < 2; i++) {
        s_cam_buf[i] = heap_caps_malloc(buf_size, MALLOC_CAP_8BIT | MALLOC_CAP_SPIRAM);
        if (s_cam_buf[i] == NULL) {
            ESP_LOGE(TAG, "alloc camera lvgl buffer[%d] failed", i);
            return ESP_ERR_NO_MEM;
        }
        memset(s_cam_buf[i], 0, buf_size);

        s_cam_img_dsc[i].header.magic = LV_IMAGE_HEADER_MAGIC;
        s_cam_img_dsc[i].header.cf = LV_COLOR_FORMAT_RGB565;
        s_cam_img_dsc[i].header.w = CAM_FRAME_W;
        s_cam_img_dsc[i].header.h = CAM_FRAME_H;
        s_cam_img_dsc[i].header.stride = CAM_FRAME_W * sizeof(uint16_t);
        s_cam_img_dsc[i].data_size = buf_size;
        s_cam_img_dsc[i].data = (const uint8_t *)s_cam_buf[i];
    }

    return ESP_OK;
}

// AI 生成代码入口：主页面 + 摄像头 / 音乐 / 录音 / 录音播放 / SD 卡 / RGB 灯
esp_err_t bsp_ai_ui_start(void)
{
    ESP_RETURN_ON_ERROR(bsp_lcd_init(), TAG, "LCD init failed");
    ESP_RETURN_ON_ERROR(bsp_lvgl_display_init(), TAG, "LVGL display init failed");

    // 摄像头初始化（函数内部失败仅打印日志，无返回值）
    bsp_camera_init();

    // 音频初始化（SD 卡挂载失败不阻塞 UI，仅影响曲目列表）
    esp_err_t audio_err = bsp_ai_audio_init();
    if (audio_err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed: %s", esp_err_to_name(audio_err));
    }

    // RGB 灯初始化并从 SD 卡恢复上次保存的颜色
    esp_err_t rgb_err = bsp_ai_rgb_init();
    if (rgb_err != ESP_OK) {
        ESP_LOGW(TAG, "rgb init failed: %s", esp_err_to_name(rgb_err));
    }

    // WiFi 初始化：连接记录存在 SD 卡中，启动后尝试自动连接
    esp_err_t wifi_err = bsp_ai_wifi_init();
    if (wifi_err != ESP_OK) {
        ESP_LOGW(TAG, "wifi init failed: %s", esp_err_to_name(wifi_err));
    } else if (bsp_ai_wifi_auto_connect_start() != ESP_OK) {
        ESP_LOGW(TAG, "wifi auto connect start failed");
    }

    // 小智语音助手：等待 WiFi 后获取激活信息并连接官方云
    esp_err_t ai_err = bsp_ai_xz_init();
    if (ai_err != ESP_OK) {
        ESP_LOGW(TAG, "ai init failed: %s", esp_err_to_name(ai_err));
    }

    lvgl_port_lock(0);

    esp_err_t err = bsp_ai_alloc_camera_buffers();
    if (err != ESP_OK) {
        lvgl_port_unlock();
        return err;
    }

    s_main_screen = bsp_ai_create_main_screen();
    s_camera_screen = bsp_ai_create_camera_screen();

    // 抓帧定时器先暂停，进入摄像头页面后再启动
    s_cam_timer = lv_timer_create(cam_lvgl_timer_cb, CAM_REFRESH_PERIOD_MS, NULL);
    if (s_cam_timer == NULL) {
        ESP_LOGE(TAG, "create camera refresh timer failed");
        lvgl_port_unlock();
        return ESP_FAIL;
    }
    lv_timer_pause(s_cam_timer);

    // 默认显示主页面
    lv_screen_load(s_main_screen);

    lvgl_port_unlock();

    show_heap_sram_psram();

    return ESP_OK;
}
