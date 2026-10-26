// bsp_ai_xz.c — 小智官方云接入：激活、WebSocket 会话、Opus 半双工音频
#include "bsp_ai_xz.h"

#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "bsp_ai_audio.h"
#include "bsp_ai_record.h"
#include "bsp_ai_wifi.h"
#include "esp_audio_dec.h"
#include "esp_audio_enc.h"
#include "esp_check.h"
#include "esp_event.h"
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_mcp_engine.h"
#include "esp_opus_dec.h"
#include "esp_opus_enc.h"
#include "esp_system.h"
#include "esp_xiaozhi_chat.h"
#include "esp_xiaozhi_info.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/semphr.h"
#include "freertos/stream_buffer.h"
#include "freertos/task.h"

static const char *TAG = "bsp_ai_xz";

#define XZ_UPLINK_RATE      (16000)
#define XZ_DOWNLINK_RATE    (24000)
// 播放侧统一到 16kHz，才能和 ES7210 采集共用同一 I2S 时钟做全双工（边听边说）
#define XZ_PLAYBACK_RATE    (16000)
#define XZ_FRAME_MS         (60)
#define XZ_MIC_BYTES        (XZ_UPLINK_RATE * 2 * 2 * XZ_FRAME_MS / 1000)
#define XZ_MONO_BYTES       (XZ_UPLINK_RATE * 2 * XZ_FRAME_MS / 1000)
#define XZ_OPUS_MAX         (1500)
#define XZ_PCM_MAX          (8192)
#define XZ_DEC_SB_SIZE      (128 * 1024)
#define XZ_MAX_FRAMES       (100)
#define XZ_SILENCE_FRAMES   (20)
#define XZ_TTS_STOP_BIT     (BIT0)
#define XZ_CONNECTED_BIT    (BIT1)
#define XZ_LINK_BIT         (BIT2)
#define XZ_ACTIVE_BIT       (BIT3)

static SemaphoreHandle_t s_lock;
static EventGroupHandle_t s_events;
static TaskHandle_t s_task;
static TaskHandle_t s_enc_task;
static SemaphoreHandle_t s_enc_req;
static SemaphoreHandle_t s_enc_done;
static int16_t *s_enc_pcm;
static size_t s_enc_pcm_bytes;
static uint8_t *s_enc_dst;
static size_t s_enc_dst_bytes;
static esp_xiaozhi_chat_handle_t s_chat;
static esp_mcp_t *s_mcp;
static void *s_enc;
static void *s_dec;
static uint8_t *s_dec_pcm;
static int16_t *s_dec_stereo;
static StreamBufferHandle_t s_dec_sb;
static uint8_t *s_dec_storage;
static StaticStreamBuffer_t s_dec_sb_struct;
static TaskHandle_t s_dec_task;
static volatile bool s_dec_busy;
static volatile uint32_t s_last_frame_ms;
static uint32_t s_drop_frames;
static uint32_t s_dec_frames;
static uint32_t s_dec_bytes;
static bool s_ready;
static bool s_channel_open;
static volatile bool s_active;
static volatile bool s_abort;
static bool s_mic_open;
static volatile bool s_pcm_open;
static volatile int s_state = BSP_AI_XZ_WAIT_WIFI;
static char s_status[64] = "等待 WiFi";
static char s_code[16] = "------";
static char s_user[160];
static char s_reply[320];

static void copy_text(char *dst, size_t size, const char *src)
{
    if (src == NULL) {
        src = "";
    }
    snprintf(dst, size, "%s", src);
}

static void set_status(int state, const char *fmt, ...)
{
    va_list args;
    va_start(args, fmt);
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    s_state = state;
    vsnprintf(s_status, sizeof(s_status), fmt, args);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
    va_end(args);
}

static void set_code(const char *code)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    copy_text(s_code, sizeof(s_code), code);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void set_user(const char *text)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    copy_text(s_user, sizeof(s_user), text);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void set_reply(const char *text)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    copy_text(s_reply, sizeof(s_reply), text);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

void bsp_ai_xz_snapshot(bsp_ai_xz_snapshot_t *out)
{
    if (out == NULL) {
        return;
    }
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    out->state = s_state;
    out->ready = s_ready;
    copy_text(out->status, sizeof(out->status), s_status);
    copy_text(out->code, sizeof(out->code), s_code);
    copy_text(out->user, sizeof(out->user), s_user);
    copy_text(out->reply, sizeof(out->reply), s_reply);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static bool ensure_decoder(void)
{
    if (s_dec != NULL) {
        return true;
    }
    esp_opus_dec_cfg_t cfg = {
        .sample_rate = XZ_DOWNLINK_RATE,
        .channel = 1,
        .frame_duration = ESP_OPUS_DEC_FRAME_DURATION_60_MS,
        .self_delimited = false,
    };
    return esp_opus_dec_open(&cfg, sizeof(cfg), &s_dec) == ESP_AUDIO_ERR_OK;
}

static esp_err_t open_pcm(void)
{
    if (s_pcm_open) {
        return ESP_OK;
    }
    esp_err_t err = bsp_ai_audio_pcm_open(XZ_PLAYBACK_RATE, 2, 16);
    if (err == ESP_OK) {
        s_pcm_open = true;
    }
    return err;
}

static void close_pcm(void)
{
    if (s_pcm_open) {
        bsp_ai_audio_pcm_close();
        s_pcm_open = false;
    }
}

// Opus 解码同样吃栈，放到 PSRAM 栈任务里，避免压爆 WebSocket 任务栈
static void xz_dec_task(void *arg)
{
    (void)arg;
    static uint8_t opus[XZ_OPUS_MAX];
    // 解码缓冲放 PSRAM，避免占用本就紧张的内部 RAM（TLS 握手需要 ~31KB 连续块）
    if (s_dec_pcm == NULL) {
        s_dec_pcm = heap_caps_malloc(XZ_PCM_MAX, MALLOC_CAP_SPIRAM);
        s_dec_stereo = heap_caps_malloc(XZ_PCM_MAX, MALLOC_CAP_SPIRAM);
    }
    if (s_dec_pcm == NULL || s_dec_stereo == NULL) {
        ESP_LOGE(TAG, "dec buffers alloc failed");
        vTaskDelete(NULL);
    }
    if (!ensure_decoder()) {
        ESP_LOGE(TAG, "opus decoder open failed");
    }
    for (;;) {
        uint16_t len = 0;
        if (xStreamBufferReceive(s_dec_sb, &len, sizeof(len), portMAX_DELAY) != sizeof(len)) {
            continue;
        }
        if (len == 0 || len > XZ_OPUS_MAX) {
            continue;
        }
        size_t got = xStreamBufferReceive(s_dec_sb, opus, len, pdMS_TO_TICKS(200));
        if (got != len || s_dec == NULL) {
            continue;
        }
        if (!s_pcm_open) {
            // 功放已关闭（上一轮结束或新一轮采集），迟到的帧直接丢弃，不再逐帧解码/告警
            continue;
        }
        s_dec_busy = true;
        esp_audio_dec_in_raw_t raw = {.buffer = opus, .len = len};
        esp_audio_dec_out_frame_t out = {.buffer = s_dec_pcm, .len = XZ_PCM_MAX};
        esp_audio_dec_info_t info = {0};
        esp_audio_err_t derr = esp_opus_dec_decode(s_dec, &raw, &out, &info);
        static uint32_t dec_frames;
        if (derr == ESP_AUDIO_ERR_OK && out.decoded_size > 0 && s_pcm_open) {
            int16_t *mono = (int16_t *)s_dec_pcm;
            size_t in_samples = out.decoded_size / sizeof(int16_t);
            // 24kHz -> 16kHz 线性插值，保证播放与麦克风同采样率、可全双工
            size_t out_samples = in_samples * XZ_PLAYBACK_RATE / XZ_DOWNLINK_RATE;
            int peak = 0;
            for (size_t i = 0; i < out_samples; i++) {
                size_t num = i * XZ_DOWNLINK_RATE;
                size_t i0 = num / XZ_PLAYBACK_RATE;
                size_t rem = num % XZ_PLAYBACK_RATE;
                if (i0 >= in_samples) {
                    i0 = in_samples - 1;
                    rem = 0;
                }
                int32_t s0 = mono[i0];
                int32_t s1 = (i0 + 1 < in_samples) ? mono[i0 + 1] : s0;
                int32_t v = (s0 * (XZ_PLAYBACK_RATE - (int32_t)rem) + s1 * (int32_t)rem) /
                            XZ_PLAYBACK_RATE;
                int16_t s = (int16_t)v;
                int av = (s < 0) ? -s : s;
                if (av > peak) {
                    peak = av;
                }
                s_dec_stereo[i * 2] = s;
                s_dec_stereo[i * 2 + 1] = s;
            }
            esp_err_t werr = bsp_ai_audio_pcm_write(s_dec_stereo, out_samples * 4);
            dec_frames++;
            s_dec_frames++;
            s_dec_bytes += (uint32_t)(out_samples * 4);
            if (dec_frames <= 3 || (dec_frames % 100) == 0 || werr != ESP_OK) {
                ESP_LOGI(TAG, "dec %u: %u -> %u bytes peak=%d write=%s", (unsigned)dec_frames,
                         (unsigned)len, (unsigned)(out_samples * 4), peak, esp_err_to_name(werr));
            }
        } else {
            ESP_LOGW(TAG, "dec failed: err=%d len=%u pcm=%d", (int)derr, (unsigned)len,
                     (int)s_pcm_open);
        }
        s_dec_busy = false;
    }
}

// TTS Opus 帧只入队，解码与写 ES8311 由 xz_dec_task 完成
static void xz_audio_cb(const uint8_t *data, int len, void *ctx)
{
    (void)ctx;
    if (data == NULL || len <= 0 || len > XZ_OPUS_MAX || s_dec_sb == NULL) {
        return;
    }
    uint16_t n = (uint16_t)len;
    static uint32_t frames;
    frames++;
    s_last_frame_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    if (frames <= 3 || (frames % 100) == 0) {
        ESP_LOGI(TAG, "tts frame %u len=%d pcm_open=%d", (unsigned)frames, len, (int)s_pcm_open);
    }
    if (xStreamBufferSpacesAvailable(s_dec_sb) < (size_t)n + sizeof(n)) {
        s_drop_frames++;
        if (s_drop_frames <= 3 || (s_drop_frames % 50) == 0) {
            ESP_LOGW(TAG, "tts buffer full, dropped %u frame(s)", (unsigned)s_drop_frames);
        }
        return;
    }
    (void)xStreamBufferSend(s_dec_sb, &n, sizeof(n), 0);
    (void)xStreamBufferSend(s_dec_sb, data, (size_t)n, 0);
}

// 云端在设备未绑定时会下发英文提示，解析出末尾的 6 位激活码
static void parse_binding_prompt(const char *text)
{
    const char *p = strstr(text, "verification code");
    if (p == NULL) {
        return;
    }
    p = strchr(p, ':');
    if (p == NULL) {
        return;
    }
    p++;
    while (*p == ' ') {
        p++;
    }
    char code[16] = {0};
    size_t len = 0;
    while (p[len] >= '0' && p[len] <= '9' && len < sizeof(code) - 1) {
        code[len] = p[len];
        len++;
    }
    if (len < 4) {
        return;
    }
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    snprintf(s_code, sizeof(s_code), "%s", code);
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
    set_status(BSP_AI_XZ_BINDING, "设备未绑定，请在 xiaozhi.me 输入激活码");
    ESP_LOGI(TAG, "binding code: %s", code);
}

static void xz_event_cb(esp_xiaozhi_chat_event_t event, void *event_data, void *ctx)
{
    (void)ctx;
    if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_TEXT && event_data != NULL) {
        esp_xiaozhi_chat_text_data_t *text = (esp_xiaozhi_chat_text_data_t *)event_data;
        ESP_LOGI(TAG, "text role=%d: %s", (int)text->role, text->text);
        if (text->role == ESP_XIAOZHI_CHAT_TEXT_ROLE_USER) {
            set_user(text->text);
        } else {
            set_reply(text->text);
            // 绑定完成后，云端不再下发激活提示；收到正常回复即认为绑定已完成
            if (s_code[0] != '-' && strstr(text->text, "verification code") == NULL &&
                strstr(text->text, "绑定") == NULL) {
                snprintf(s_code, sizeof(s_code), "------");
                set_status(BSP_AI_XZ_READY, "请说话，我在听...");
            }
            parse_binding_prompt(text->text);
        }
    } else if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_TTS_STATE && event_data != NULL) {
        esp_xiaozhi_chat_tts_state_t *state = (esp_xiaozhi_chat_tts_state_t *)event_data;
        ESP_LOGI(TAG, "tts state=%d", (int)state->state);
        if (state->state == ESP_XIAOZHI_CHAT_TTS_STATE_START) {
            set_status(BSP_AI_XZ_SPEAKING, "正在说话...");
        } else if (state->state == ESP_XIAOZHI_CHAT_TTS_STATE_STOP) {
            xEventGroupSetBits(s_events, XZ_TTS_STOP_BIT);
        }
    } else if (event == ESP_XIAOZHI_CHAT_EVENT_CHAT_ERROR) {
        ESP_LOGW(TAG, "chat error event");
        set_status(BSP_AI_XZ_ERROR, "对话出错");
    }
}

static bool wait_wifi(void)
{
    for (int i = 0; i < 120; i++) {
        if (bsp_ai_wifi_connected()) {
            return true;
        }
        if ((i % 10) == 0) {
            ESP_LOGI(TAG, "waiting wifi, free=%u", (unsigned)esp_get_free_heap_size());
        }
        vTaskDelay(pdMS_TO_TICKS(500));
    }
    return false;
}

static esp_err_t fetch_config(void)
{
    for (int attempt = 0; attempt < 120; attempt++) {
        esp_xiaozhi_chat_info_t info = {0};
        ESP_LOGI(TAG, "fetch attempt %d, free=%u int_free=%u int_max=%u", attempt,
                 (unsigned)esp_get_free_heap_size(),
                 (unsigned)heap_caps_get_free_size(MALLOC_CAP_INTERNAL),
                 (unsigned)heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL));
        esp_err_t err = esp_xiaozhi_chat_get_info(&info);
        if (err != ESP_OK) {
            ESP_LOGW(TAG, "get info failed: %s", esp_err_to_name(err));
        }
        if (err == ESP_OK && info.has_websocket_config) {
            esp_xiaozhi_chat_free_info(&info);
            return ESP_OK;
        }
        if (info.has_activation_code) {
            ESP_LOGI(TAG, "activation code: %s", info.activation_code);
            set_code(info.activation_code);
            set_status(BSP_AI_XZ_BINDING, "请在 xiaozhi.me 绑定");
        } else {
            set_status(BSP_AI_XZ_ACTIVATING, "正在获取激活信息...");
        }
        esp_xiaozhi_chat_free_info(&info);
        vTaskDelay(pdMS_TO_TICKS(5000));
    }
    return ESP_FAIL;
}

static void xz_chat_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    (void)base;
    (void)data;
    if (id == ESP_XIAOZHI_CHAT_EVENT_CONNECTED) {
        xEventGroupSetBits(s_events, XZ_CONNECTED_BIT);
        if (!s_channel_open) {
            xEventGroupSetBits(s_events, XZ_LINK_BIT);
        }
    } else if (id == ESP_XIAOZHI_CHAT_EVENT_DISCONNECTED) {
        xEventGroupClearBits(s_events, XZ_CONNECTED_BIT);
        s_channel_open = false;
        s_ready = false;
        set_status(BSP_AI_XZ_ERROR, "连接断开，正在重连...");
    }
}

static esp_err_t open_channel(void)
{
    esp_xiaozhi_chat_audio_t audio = {
        .format = "opus",
        .sample_rate = XZ_UPLINK_RATE,
        .channels = 1,
        .frame_duration = XZ_FRAME_MS,
    };
    esp_err_t err = esp_xiaozhi_chat_open_audio_channel(s_chat, &audio, NULL, 0);
    if (err == ESP_OK) {
        s_channel_open = true;
        xEventGroupClearBits(s_events, XZ_LINK_BIT);
    }
    return err;
}

static esp_err_t start_chat(void)
{
    ESP_RETURN_ON_FALSE(esp_mcp_create(&s_mcp) == ESP_OK, ESP_FAIL, TAG, "mcp create failed");

    esp_xiaozhi_chat_config_t cfg = ESP_XIAOZHI_CHAT_DEFAULT_CONFIG();
    cfg.audio_callback = xz_audio_cb;
    cfg.event_callback = xz_event_cb;
    cfg.mcp_engine = s_mcp;
    cfg.owns_mcp_engine = true;
    cfg.has_mqtt_config = false;
    cfg.has_websocket_config = true;
    ESP_RETURN_ON_ERROR(esp_xiaozhi_chat_init(&cfg, &s_chat), TAG, "chat init failed");
    ESP_RETURN_ON_ERROR(esp_xiaozhi_chat_start(s_chat), TAG, "chat start failed");
    set_status(BSP_AI_XZ_ACTIVATING, "正在连接小智云端...");
    EventBits_t bits = xEventGroupWaitBits(s_events, XZ_CONNECTED_BIT, pdFALSE, pdTRUE,
                                           pdMS_TO_TICKS(15000));
    ESP_RETURN_ON_FALSE((bits & XZ_CONNECTED_BIT) != 0, ESP_ERR_TIMEOUT, TAG,
                        "websocket connect timeout");

    ESP_RETURN_ON_ERROR(open_channel(), TAG, "open audio channel failed");

    esp_opus_enc_config_t enc = {
        .sample_rate = XZ_UPLINK_RATE,
        .channel = 1,
        .bits_per_sample = 16,
        .bitrate = 24000,
        .frame_duration = ESP_OPUS_ENC_FRAME_DURATION_60_MS,
        .application_mode = ESP_OPUS_ENC_APPLICATION_VOIP,
        .complexity = 0,
        .enable_fec = false,
        .enable_dtx = false,
        .enable_vbr = false,
    };
    ESP_RETURN_ON_FALSE(esp_opus_enc_open(&enc, sizeof(enc), &s_enc) == ESP_AUDIO_ERR_OK,
                        ESP_FAIL, TAG, "opus enc open failed");
    return ESP_OK;
}

// WS 断线重连后必须重新发 hello、重开音频通道，否则会话是死的
static void reopen_channel(void)
{
    if (s_chat == 0) {
        return;
    }
    s_ready = false;
    (void)esp_xiaozhi_chat_close_audio_channel(s_chat);
    s_channel_open = false;
    vTaskDelay(pdMS_TO_TICKS(300));
    if (open_channel() == ESP_OK) {
        s_ready = true;
        set_status(BSP_AI_XZ_READY, "请说话，我在听...");
        ESP_LOGI(TAG, "audio channel reopened");
    } else {
        set_status(BSP_AI_XZ_ERROR, "重连小智云端失败");
    }
}

// 每次读取 60ms 立体声，取左声道得到单声道帧
static size_t read_mono(int16_t *mono, size_t frames)
{
    static uint8_t mic[XZ_MIC_BYTES];
    size_t need = frames * 4;
    size_t got = 0, total = 0;
    while (total < need) {
        if (bsp_ai_record_pcm_read(mic + total, need - total, &got) != ESP_OK || got == 0) {
            return 0;
        }
        total += got;
    }
    int16_t *src = (int16_t *)mic;
    for (size_t i = 0; i < frames; i++) {
        mono[i] = src[i * 2];
    }
    return frames * sizeof(int16_t);
}

// Opus 编码栈很深（约 19KB），放到 PSRAM 栈任务里执行，避免占用内部 RAM
static void xz_opus_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_enc_req, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        s_enc_dst_bytes = 0;
        if (s_enc != NULL && s_enc_pcm != NULL && s_enc_dst != NULL) {
            esp_audio_enc_in_frame_t in = {.buffer = (uint8_t *)s_enc_pcm,
                                           .len = (uint32_t)s_enc_pcm_bytes};
            esp_audio_enc_out_frame_t out = {.buffer = s_enc_dst, .len = XZ_OPUS_MAX};
            if (esp_opus_enc_process(s_enc, &in, &out) == ESP_AUDIO_ERR_OK) {
                s_enc_dst_bytes = out.encoded_bytes;
            }
        }
        xSemaphoreGive(s_enc_done);
    }
}

static int capture_and_send(void)
{
    static int16_t mono[XZ_MONO_BYTES / sizeof(int16_t)];
    static uint8_t opus[XZ_OPUS_MAX];
    int frames = 0, silence = 0, sent = 0;
    bool speech = false;

    // 连续对话期间麦克风保持常开，避免每轮反复 open/close 造成 I2S 重配和丢帧
    if (!s_mic_open) {
        (void)bsp_ai_audio_stop();
        if (bsp_ai_record_pcm_open() != ESP_OK) {
            return -1;
        }
        s_mic_open = true;
    }
    if (esp_xiaozhi_chat_send_start_listening(s_chat, ESP_XIAOZHI_CHAT_LISTENING_MODE_AUTO) != ESP_OK) {
        return -1;
    }

    while (frames < XZ_MAX_FRAMES && !s_abort) {
        size_t bytes = read_mono(mono, XZ_MONO_BYTES / sizeof(int16_t));
        if (bytes == 0) {
            break;
        }
        int64_t sum = 0;
        for (size_t i = 0; i < bytes / sizeof(int16_t); i++) {
            sum += (int32_t)mono[i] * mono[i];
        }
        int rms = (int)(sum / (bytes / sizeof(int16_t)));

        if (!speech) {
            frames++;
            if (rms <= 300000) {
                // 连续对话时环境噪声很常见，检测到人声之前不上传，避免云端把静音当成一句话
                if (frames >= 60) {
                    break;
                }
                continue;
            }
            speech = true;
        }

        if (rms < 50000) {
            silence++;
        } else {
            silence = 0;
        }

        s_enc_pcm = mono;
        s_enc_pcm_bytes = bytes;
        s_enc_dst = opus;
        s_enc_dst_bytes = 0;
        xSemaphoreGive(s_enc_req);
        if (xSemaphoreTake(s_enc_done, pdMS_TO_TICKS(1000)) == pdTRUE && s_enc_dst_bytes > 0) {
            (void)esp_xiaozhi_chat_send_audio_data(s_chat, (const char *)opus, s_enc_dst_bytes);
            sent++;
        }
        frames++;
        if (silence >= XZ_SILENCE_FRAMES) {
            break;
        }
    }

    if (!speech || sent == 0) {
        // 没听到人声：麦克风保持打开，下轮直接继续监听
        (void)esp_xiaozhi_chat_send_stop_listening(s_chat);
        return 0;
    }

    set_status(BSP_AI_XZ_THINKING, "正在思考...");
    // 丢弃上一轮被打断后残留的 TTS 帧，避免打断后先播旧内容
    if (s_dec_sb != NULL) {
        (void)xStreamBufferReset(s_dec_sb);
    }
    if (open_pcm() != ESP_OK) {
        ESP_LOGW(TAG, "open tts pcm failed");
    }
    (void)esp_xiaozhi_chat_send_stop_listening(s_chat);
    return sent;
}

// 等待本轮 TTS 播放结束；不做插话检测，用户等播完再说话
static void wait_tts_done(void)
{
    for (int i = 0; i < 30000 && !s_abort; i++) {
        EventBits_t bits = xEventGroupWaitBits(s_events, XZ_TTS_STOP_BIT, pdTRUE, pdFALSE, 0);
        if (bits & XZ_TTS_STOP_BIT) {
            return;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    if (!s_abort) {
        ESP_LOGW(TAG, "tts wait timeout, still playing");
    }
}

static void close_mic(void)
{
    if (s_mic_open) {
        bsp_ai_record_pcm_close();
        s_mic_open = false;
    }
}

static void run_turn(void)
{
    s_abort = false;
    s_last_frame_ms = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
    s_dec_frames = 0;
    s_dec_bytes = 0;
    set_status(BSP_AI_XZ_LISTENING, "正在聆听...");
    int sent = capture_and_send();
    if (sent < 0) {
        set_status(BSP_AI_XZ_ERROR, "麦克风打开失败");
        close_pcm();
        return;
    }
    if (sent == 0 || s_abort) {
        close_pcm();
        if (!s_abort && s_active) {
            set_status(BSP_AI_XZ_READY, "请说话，我在听...");
        }
        return;
    }

    xEventGroupClearBits(s_events, XZ_TTS_STOP_BIT);
    wait_tts_done();

    // TTS 正常结束：等解码队列真正放完再关功放
    for (int i = 0; i < 60; i++) {
        if (xStreamBufferIsEmpty(s_dec_sb) && !s_dec_busy) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(10));
    }
    uint32_t quiet_since = 0;
    for (int i = 0; i < 400 && !s_abort; i++) {
        uint32_t now = (uint32_t)(xTaskGetTickCount() * portTICK_PERIOD_MS);
        bool idle = xStreamBufferIsEmpty(s_dec_sb) && !s_dec_busy;
        if (idle) {
            if (quiet_since == 0) {
                quiet_since = now;
            }
        } else {
            quiet_since = 0;
        }
        bool no_new_frame = (now - s_last_frame_ms) > 800;
        if (quiet_since != 0 && (now - quiet_since) > 800 && no_new_frame) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    close_pcm();
    if (!s_active) {
        close_mic();
    }
    ESP_LOGI(TAG, "turn done: dec=%u bytes=%u drop=%u", (unsigned)s_dec_frames,
             (unsigned)s_dec_bytes, (unsigned)s_drop_frames);
    if (s_abort) {
        return;
    }
    if (s_code[0] == '-') {
        set_status(BSP_AI_XZ_READY, "请说话，我在听...");
    } else {
        set_status(BSP_AI_XZ_BINDING, "设备未绑定，请在 xiaozhi.me 输入激活码");
    }
}

static void xz_task(void *arg)
{
    (void)arg;
    if (!wait_wifi()) {
        set_status(BSP_AI_XZ_ERROR, "等待 WiFi 超时");
        vTaskDelete(NULL);
        return;
    }
    set_status(BSP_AI_XZ_ACTIVATING, "正在获取激活信息...");
    if (fetch_config() != ESP_OK) {
        set_status(BSP_AI_XZ_ERROR, "获取激活信息失败");
        vTaskDelete(NULL);
        return;
    }
    if (start_chat() != ESP_OK) {
        set_status(BSP_AI_XZ_ERROR, "连接小智云端失败");
        vTaskDelete(NULL);
        return;
    }

    s_ready = true;
    set_status(BSP_AI_XZ_READY, "请说话，我在听...");
    ESP_LOGI(TAG, "chat ready, free=%u", (unsigned)esp_get_free_heap_size());
    for (;;) {
        if (!s_active) {
            // 页面不在前台：挂起，等待页面重新进入
            xEventGroupClearBits(s_events, XZ_ACTIVE_BIT);
            (void)xEventGroupWaitBits(s_events, XZ_ACTIVE_BIT, pdTRUE, pdFALSE, portMAX_DELAY);
            continue;
        }
        if (!s_ready) {
            // 连接断开：等待重连完成并重开音频通道
            EventBits_t bits = xEventGroupWaitBits(s_events, XZ_LINK_BIT, pdTRUE, pdFALSE,
                                                   pdMS_TO_TICKS(2000));
            if ((bits & XZ_LINK_BIT) != 0) {
                reopen_channel();
            }
            continue;
        }
        if (s_code[0] != '-') {
            // 未绑定：仍然允许说话，云端会继续下发绑定提示；绑定成功后收到正常回复即自动放行
            set_status(BSP_AI_XZ_BINDING, "设备未绑定，请在 xiaozhi.me 输入激活码");
        }
        run_turn();
        vTaskDelay(pdMS_TO_TICKS(250));
    }
}

esp_err_t bsp_ai_xz_init(void)
{
    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    s_enc_req = xSemaphoreCreateBinary();
    s_enc_done = xSemaphoreCreateBinary();
    ESP_RETURN_ON_FALSE(s_lock != NULL && s_events != NULL && s_enc_req != NULL && s_enc_done != NULL,
                        ESP_ERR_NO_MEM, TAG, "no mem");
    ESP_RETURN_ON_ERROR(esp_event_handler_register(ESP_XIAOZHI_CHAT_EVENTS, ESP_EVENT_ANY_ID,
                                                   xz_chat_event_handler, NULL),
                        TAG, "register event failed");

    BaseType_t enc_ok = xTaskCreateWithCaps(xz_opus_task, "xz_opus", 32768, NULL, 5, &s_enc_task,
                                            MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(enc_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create opus task failed");

    s_dec_storage = heap_caps_malloc(XZ_DEC_SB_SIZE, MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(s_dec_storage != NULL, ESP_ERR_NO_MEM, TAG, "no dec buffer");
    s_dec_sb = xStreamBufferCreateStatic(XZ_DEC_SB_SIZE, 1, s_dec_storage, &s_dec_sb_struct);
    ESP_RETURN_ON_FALSE(s_dec_sb != NULL, ESP_ERR_NO_MEM, TAG, "create dec buffer failed");
    BaseType_t dec_ok = xTaskCreateWithCaps(xz_dec_task, "xz_dec", 32768, NULL, 5, &s_dec_task,
                                            MALLOC_CAP_SPIRAM);
    ESP_RETURN_ON_FALSE(dec_ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create dec task failed");

    BaseType_t ok = xTaskCreate(xz_task, "xz_task", 12288, NULL, 5, &s_task);
    ESP_RETURN_ON_FALSE(ok == pdPASS, ESP_ERR_NO_MEM, TAG, "create task failed");
    ESP_LOGI(TAG, "xiaozhi assistant starting");
    return ESP_OK;
}

void bsp_ai_xz_set_active(bool active)
{
    s_active = active;
    if (active) {
        xEventGroupSetBits(s_events, XZ_ACTIVE_BIT);
        return;
    }
    s_abort = true;
    xEventGroupClearBits(s_events, XZ_ACTIVE_BIT);
    (void)bsp_ai_audio_stop();
    close_pcm();
    close_mic();
}

void bsp_ai_xz_stop(void)
{
    s_abort = true;
    if (s_chat != 0) {
        (void)esp_xiaozhi_chat_send_abort_speaking(s_chat,
                                                   ESP_XIAOZHI_CHAT_ABORT_SPEAKING_REASON_STOP_LISTENING);
    }
    (void)bsp_ai_audio_stop();
    close_pcm();
    close_mic();
    if (s_ready) {
        set_status(BSP_AI_XZ_READY, "请说话，我在听...");
    }
}
