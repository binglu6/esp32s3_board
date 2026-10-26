// bsp_ai_audio.c — SD 卡曲目扫描 + ES8311/I2S 播放 + 播放状态管理
#include "bsp_ai_audio.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>

#include "audio_player.h"
#include "bsp_ai_sdcard.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_pca9557.h"

static const char *TAG = "bsp_ai_audio";

// I2S / ES8311 参数（与 esp32s3_core 的 bsp_es8311 保持一致）
#define BSP_AI_I2S_NUM          (0)
#define BSP_AI_I2S_MCK_IO       (48)
#define BSP_AI_I2S_BCK_IO       (21)
#define BSP_AI_I2S_WS_IO        (14)
#define BSP_AI_I2S_DO_IO        (47)
#define BSP_AI_I2S_DI_IO        (13)
#define BSP_AI_PA_CTRL_IO       (-1)

#define BSP_AI_SAMPLE_RATE      (16000)
#define BSP_AI_BIT_WIDTH        (32)
#define BSP_AI_CHANNEL          (2)

#define BSP_AI_VOLUME_MIN       (0)
#define BSP_AI_VOLUME_MAX       (100)
#define BSP_AI_VOLUME_DEFAULT   (60)

#define BSP_AI_PATH_LEN         (300)
#define BSP_AI_NAME_LEN         (128)

typedef struct {
    char path[BSP_AI_PATH_LEN];
    char name[BSP_AI_NAME_LEN];
} bsp_ai_track_t;

static bsp_ai_track_t s_tracks[BSP_AI_AUDIO_MAX_TRACKS];
static size_t s_count = 0;
static size_t s_index = 0;
static bsp_ai_audio_src_t s_source = BSP_AI_AUDIO_SRC_MUSIC;

static volatile bool s_playing = false;
static bool s_external = false; // 播放列表之外的单文件（AI 回复），结束后不自动切歌
static bool s_started = false;
static bool s_ready = false;
static bool s_inited = false;
static int s_volume = BSP_AI_VOLUME_DEFAULT;

static i2s_chan_handle_t s_tx_chan = NULL;
static i2s_chan_handle_t s_rx_chan = NULL;
static esp_codec_dev_handle_t s_play_dev = NULL;
static bool s_dev_open = false;
static esp_codec_dev_sample_info_t s_dev_fs = {
    .sample_rate = BSP_AI_SAMPLE_RATE,
    .channel = 2,
    .bits_per_sample = BSP_AI_BIT_WIDTH,
};

// 去掉目录与扩展名，得到列表/标题显示用名称
static void track_display_name(const char *file_name, char *out, size_t out_len)
{
    snprintf(out, out_len, "%s", file_name);
    char *dot = strrchr(out, '.');
    if (dot != NULL) {
        *dot = '\0';
    }
}

static bool has_audio_ext(const char *file_name)
{
    const char *dot = strrchr(file_name, '.');
    if (dot == NULL) {
        return false;
    }
    return strcasecmp(dot, ".mp3") == 0 || strcasecmp(dot, ".wav") == 0;
}

static void scan_dir(const char *dir)
{
    DIR *dp = opendir(dir);
    if (dp == NULL) {
        ESP_LOGD(TAG, "open dir %s failed", dir);
        return;
    }

    struct dirent *ent = NULL;
    while ((ent = readdir(dp)) != NULL && s_count < BSP_AI_AUDIO_MAX_TRACKS) {
        if (ent->d_name[0] == '.' || !has_audio_ext(ent->d_name)) {
            continue;
        }

        bsp_ai_track_t *track = &s_tracks[s_count];
        snprintf(track->path, sizeof(track->path), "%s/%s", dir, ent->d_name);
        track_display_name(ent->d_name, track->name, sizeof(track->name));
        s_count++;
        ESP_LOGI(TAG, "track[%u]: %s", (unsigned)s_count, track->name);
    }
    closedir(dp);
}

static int track_cmp(const void *a, const void *b)
{
    const bsp_ai_track_t *ta = (const bsp_ai_track_t *)a;
    const bsp_ai_track_t *tb = (const bsp_ai_track_t *)b;
    return strcasecmp(ta->name, tb->name);
}

static const char *source_dir(void)
{
    return (s_source == BSP_AI_AUDIO_SRC_RECORD) ? BSP_AI_REC_DIR : BSP_AI_MUSIC_DIR;
}

// ---------------- 音频硬件 ----------------

static esp_err_t i2s_init(void)
{
    i2s_chan_config_t chan_cfg = I2S_CHANNEL_DEFAULT_CONFIG(BSP_AI_I2S_NUM, I2S_ROLE_MASTER);
    chan_cfg.auto_clear = true;
    ESP_RETURN_ON_ERROR(i2s_new_channel(&chan_cfg, &s_tx_chan, &s_rx_chan),
                        TAG, "create I2S tx/rx channel failed");

    const i2s_std_config_t std_cfg = {
        .clk_cfg = I2S_STD_CLK_DEFAULT_CONFIG(BSP_AI_SAMPLE_RATE),
        .slot_cfg = I2S_STD_PHILIPS_SLOT_DEFAULT_CONFIG(BSP_AI_BIT_WIDTH, I2S_SLOT_MODE_STEREO),
        .gpio_cfg = {
            .mclk = BSP_AI_I2S_MCK_IO,
            .bclk = BSP_AI_I2S_BCK_IO,
            .ws = BSP_AI_I2S_WS_IO,
            .dout = BSP_AI_I2S_DO_IO,
            .din = BSP_AI_I2S_DI_IO,
        },
    };

    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_tx_chan, &std_cfg),
                        TAG, "init I2S tx std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_init_std_mode(s_rx_chan, &std_cfg),
                        TAG, "init I2S rx std mode failed");
    ESP_RETURN_ON_ERROR(i2s_channel_enable(s_tx_chan), TAG, "enable I2S tx failed");
    return ESP_OK;
}

static esp_err_t codec_init(void)
{
    ESP_RETURN_ON_ERROR(i2s_init(), TAG, "I2S init failed");

    audio_codec_i2s_cfg_t i2s_cfg = {
        .port = BSP_AI_I2S_NUM,
        .rx_handle = NULL,
        .tx_handle = s_tx_chan,
    };
    const audio_codec_data_if_t *i2s_data_if = audio_codec_new_i2s_data(&i2s_cfg);
    ESP_RETURN_ON_FALSE(i2s_data_if != NULL, ESP_FAIL, TAG, "create I2S data interface failed");

    const audio_codec_gpio_if_t *gpio_if = audio_codec_new_gpio();
    ESP_RETURN_ON_FALSE(gpio_if != NULL, ESP_FAIL, TAG, "create GPIO interface failed");

    audio_codec_i2c_cfg_t i2c_cfg = {
        .port = BSP_I2C_NUM,
        .addr = ES8311_CODEC_DEFAULT_ADDR,
        .bus_handle = i2c_bus_handle,
    };
    const audio_codec_ctrl_if_t *i2c_ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
    ESP_RETURN_ON_FALSE(i2c_ctrl_if != NULL, ESP_FAIL, TAG, "create I2C control interface failed");

    esp_codec_dev_hw_gain_t gain = {
        .pa_voltage = 5.0,
        .codec_dac_voltage = 3.3,
    };

    es8311_codec_cfg_t es8311_cfg = {
        .ctrl_if = i2c_ctrl_if,
        .gpio_if = gpio_if,
        .codec_mode = ESP_CODEC_DEV_WORK_MODE_DAC,
        .pa_pin = BSP_AI_PA_CTRL_IO,
        .pa_reverted = false,
        .master_mode = false,
        .use_mclk = true,
        .digital_mic = false,
        .invert_mclk = false,
        .invert_sclk = false,
        .hw_gain = gain,
    };
    const audio_codec_if_t *es8311_dev = es8311_codec_new(&es8311_cfg);
    ESP_RETURN_ON_FALSE(es8311_dev != NULL, ESP_FAIL, TAG, "create ES8311 codec failed");

    esp_codec_dev_cfg_t dev_cfg = {
        .dev_type = ESP_CODEC_DEV_TYPE_OUT,
        .codec_if = es8311_dev,
        .data_if = i2s_data_if,
    };
    s_play_dev = esp_codec_dev_new(&dev_cfg);
    ESP_RETURN_ON_FALSE(s_play_dev != NULL, ESP_ERR_NO_MEM, TAG, "create codec device failed");

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BSP_AI_SAMPLE_RATE,
        .channel = BSP_AI_CHANNEL,
        .bits_per_sample = BSP_AI_BIT_WIDTH,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_play_dev, &fs), TAG, "open codec device failed");
    s_dev_fs = fs;
    s_dev_open = true;
    esp_codec_dev_set_out_vol(s_play_dev, s_volume);
    return ESP_OK;
}

static esp_err_t codec_reopen(void)
{
    if (s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_dev_open) {
        return ESP_OK;
    }
    // 播放设备被 stop() 关闭后，I2S TX 可能仍为录音保持使能；
    // esp_codec_dev_open() 内部会再次 enable，先 disable 避免状态冲突。
    if (s_tx_chan != NULL) {
        (void)i2s_channel_disable(s_tx_chan);
    }
    esp_err_t err = esp_codec_dev_open(s_play_dev, &s_dev_fs);
    if (err == ESP_OK) {
        s_dev_open = true;
        esp_codec_dev_set_out_vol(s_play_dev, s_volume);
    }
    return err;
}

static esp_err_t codec_set_fs(uint32_t rate, uint32_t bits_cfg, i2s_slot_mode_t ch)
{
    if (s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    esp_codec_dev_close(s_play_dev);
    s_dev_open = false;
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = ch,
        .bits_per_sample = bits_cfg,
    };
    esp_err_t err = esp_codec_dev_open(s_play_dev, &fs);
    if (err == ESP_OK) {
        s_dev_fs = fs;
        s_dev_open = true;
        esp_codec_dev_set_out_vol(s_play_dev, s_volume);
    }
    return err;
}

static esp_err_t codec_write(void *audio_buffer, size_t len, size_t *bytes_written,
                             uint32_t timeout_ms)
{
    (void)timeout_ms;
    esp_err_t err = codec_reopen();
    if (err != ESP_OK) {
        *bytes_written = 0;
        return err;
    }
    err = esp_codec_dev_write(s_play_dev, audio_buffer, len);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec write failed: %s", esp_err_to_name(err));
    }
    *bytes_written = (err == ESP_OK) ? len : 0;
    return err;
}

static esp_err_t codec_mute(AUDIO_PLAYER_MUTE_SETTING setting)
{
    if (s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_mute(s_play_dev, setting == AUDIO_PLAYER_MUTE);
}

// ---------------- 播放控制 ----------------

static void audio_event_cb(audio_player_cb_ctx_t *ctx)
{
    switch (ctx->audio_event) {
    case AUDIO_PLAYER_CALLBACK_EVENT_IDLE:
        // AI 回复等单文件播放：播完即停，不进入列表循环。
        if (s_external) {
            s_external = false;
            s_playing = false;
            pa_en(0);
            break;
        }
        // 音乐目录：自然播放结束后自动切下一首；
        // 录音目录：播放完当前录音就停止，不循环、不自动切歌。
        if (s_source == BSP_AI_AUDIO_SRC_MUSIC && s_playing && s_count > 0) {
            size_t next = (s_index + 1) % s_count;
            if (bsp_ai_audio_play(next) != ESP_OK) {
                s_playing = false;
                pa_en(0);
            }
        } else if (s_source == BSP_AI_AUDIO_SRC_RECORD) {
            s_playing = false;
            pa_en(0);
        }
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PLAYING:
        s_playing = true;
        break;
    case AUDIO_PLAYER_CALLBACK_EVENT_PAUSE:
        s_playing = false;
        pa_en(0);
        break;
    default:
        break;
    }
}

esp_err_t bsp_ai_audio_rescan(void)
{
    if (!bsp_ai_sd_is_mounted()) {
        esp_err_t err = bsp_ai_sd_mount();
        if (err != ESP_OK) {
            return err;
        }
    }
    (void)bsp_ai_sd_prepare_dirs();

    char current[BSP_AI_PATH_LEN] = {0};
    if (s_index < s_count) {
        snprintf(current, sizeof(current), "%s", s_tracks[s_index].path);
    }

    s_count = 0;
    s_index = 0;
    scan_dir(source_dir());

    if (s_count == 0) {
        ESP_LOGW(TAG, "no playable file found in %s", source_dir());
        return ESP_ERR_NOT_FOUND;
    }

    qsort(s_tracks, s_count, sizeof(s_tracks[0]), track_cmp);

    if (current[0] != '\0') {
        for (size_t i = 0; i < s_count; i++) {
            if (strcmp(s_tracks[i].path, current) == 0) {
                s_index = i;
                break;
            }
        }
    }

    ESP_LOGI(TAG, "found %u track(s) in %s, current=%u", (unsigned)s_count, source_dir(),
             (unsigned)s_index);
    return ESP_OK;
}

esp_err_t bsp_ai_audio_select_source(bsp_ai_audio_src_t src)
{
    if (src != s_source) {
        (void)bsp_ai_audio_stop();
        s_source = src;
        s_count = 0;
        s_index = 0;
        s_started = false;
    }
    return bsp_ai_audio_rescan();
}

bsp_ai_audio_src_t bsp_ai_audio_source(void)
{
    return s_source;
}

esp_err_t bsp_ai_audio_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    (void)bsp_ai_audio_rescan();

    esp_err_t err = codec_init();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "codec init failed: %s", esp_err_to_name(err));
        return err;
    }

    audio_player_config_t player_cfg = {
        .mute_fn = codec_mute,
        .clk_set_fn = codec_set_fs,
        .write_fn = codec_write,
        .priority = 5,
        .coreID = 1,
        .force_stereo = false,
    };
    err = audio_player_new(player_cfg);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "audio_player_new failed: %s", esp_err_to_name(err));
        return err;
    }

    err = audio_player_callback_register(audio_event_cb, NULL);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "register audio callback failed: %s", esp_err_to_name(err));
        return err;
    }

    s_ready = true;
    s_inited = true;
    ESP_LOGI(TAG, "audio ready, %u track(s)", (unsigned)s_count);
    return ESP_OK;
}

size_t bsp_ai_audio_count(void)
{
    return s_count;
}

const char *bsp_ai_audio_name(size_t index)
{
    if (index >= s_count) {
        return NULL;
    }
    return s_tracks[index].name;
}

const char *bsp_ai_audio_path(size_t index)
{
    if (index >= s_count) {
        return NULL;
    }
    return s_tracks[index].path;
}

size_t bsp_ai_audio_current(void)
{
    return s_index;
}

bool bsp_ai_audio_is_playing(void)
{
    return s_playing;
}

bool bsp_ai_audio_started(void)
{
    return s_started;
}

esp_err_t bsp_ai_audio_play(size_t index)
{
    if (index >= s_count) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    FILE *fp = fopen(s_tracks[index].path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open %s failed", s_tracks[index].path);
        return ESP_FAIL;
    }

    s_external = false;
    esp_err_t err = audio_player_play(fp);
    if (err != ESP_OK) {
        // audio_player_play 失败时文件仍由调用方负责关闭
        fclose(fp);
        ESP_LOGE(TAG, "play %s failed: %s", s_tracks[index].path, esp_err_to_name(err));
        return err;
    }

    s_index = index;
    s_playing = true;
    s_started = true;
    pa_en(1);
    ESP_LOGI(TAG, "playing [%u] %s", (unsigned)index, s_tracks[index].name);
    return ESP_OK;
}

esp_err_t bsp_ai_audio_play_path(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    for (size_t i = 0; i < s_count; i++) {
        if (strcmp(s_tracks[i].path, path) == 0) {
            return bsp_ai_audio_play(i);
        }
    }
    return ESP_ERR_NOT_FOUND;
}

esp_err_t bsp_ai_audio_play_file(const char *path)
{
    if (path == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)bsp_ai_audio_stop();

    FILE *fp = fopen(path, "rb");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open %s failed", path);
        return ESP_ERR_NOT_FOUND;
    }

    s_external = true;
    esp_err_t err = audio_player_play(fp);
    if (err != ESP_OK) {
        fclose(fp);
        s_external = false;
        ESP_LOGE(TAG, "play %s failed: %s", path, esp_err_to_name(err));
        return err;
    }

    s_playing = true;
    s_started = true;
    pa_en(1);
    ESP_LOGI(TAG, "playing file %s", path);
    return ESP_OK;
}

esp_err_t bsp_ai_audio_stop(void)
{
    s_playing = false;
    s_started = false;
    s_external = false;
    pa_en(0);
    if (!s_ready) {
        return ESP_OK;
    }

    (void)audio_player_stop();
    // 等待解码任务处理完停止请求，避免关闭设备时仍有写入
    for (int i = 0; i < 50; i++) {
        audio_player_state_t st = audio_player_get_state();
        if (st != AUDIO_PLAYER_STATE_PLAYING && st != AUDIO_PLAYER_STATE_PAUSE) {
            break;
        }
        vTaskDelay(pdMS_TO_TICKS(20));
    }
    if (s_play_dev != NULL) {
        esp_codec_dev_close(s_play_dev);
        s_dev_open = false;
    }
    // 关闭播放设备会同时停止 I2S TX，而 TX 是共享 MCLK/BCLK/WS 的时钟源；
    // 录音时 ES7210 需要这些时钟才能驱动 SDOUT，因此这里保持 TX 使能。
    if (s_tx_chan != NULL) {
        (void)i2s_channel_enable(s_tx_chan);
    }
    return ESP_OK;
}

esp_err_t bsp_ai_audio_toggle(void)
{
    if (s_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    if (!s_ready) {
        return ESP_ERR_INVALID_STATE;
    }

    if (s_playing) {
        s_playing = false;
        pa_en(0);
        return audio_player_pause();
    }

    if (s_started && audio_player_get_state() == AUDIO_PLAYER_STATE_PAUSE) {
        s_playing = true;
        pa_en(1);
        return audio_player_resume();
    }

    return bsp_ai_audio_play(s_index);
}

esp_err_t bsp_ai_audio_next(void)
{
    if (s_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    return bsp_ai_audio_play((s_index + 1) % s_count);
}

esp_err_t bsp_ai_audio_prev(void)
{
    if (s_count == 0) {
        return ESP_ERR_NOT_FOUND;
    }
    return bsp_ai_audio_play((s_index + s_count - 1) % s_count);
}

void bsp_ai_audio_set_volume(int volume)
{
    if (volume < BSP_AI_VOLUME_MIN) {
        volume = BSP_AI_VOLUME_MIN;
    } else if (volume > BSP_AI_VOLUME_MAX) {
        volume = BSP_AI_VOLUME_MAX;
    }
    s_volume = volume;

    if (s_play_dev != NULL) {
        esp_codec_dev_set_out_vol(s_play_dev, s_volume);
    }
}

int bsp_ai_audio_volume(void)
{
    return s_volume;
}

i2s_chan_handle_t bsp_ai_audio_rx_handle(void)
{
    return s_rx_chan;
}

esp_err_t bsp_ai_audio_pcm_open(uint32_t rate, uint8_t channels, uint8_t bits)
{
    if (!s_ready || s_play_dev == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    (void)bsp_ai_audio_stop();

    esp_codec_dev_sample_info_t fs = {
        .sample_rate = rate,
        .channel = channels,
        .bits_per_sample = bits,
    };
    esp_err_t err = esp_codec_dev_open(s_play_dev, &fs);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "pcm open %u/%u/%u failed: %s", (unsigned)rate, channels, bits,
                 esp_err_to_name(err));
        return err;
    }

    s_dev_fs = fs;
    s_dev_open = true;
    esp_codec_dev_set_out_vol(s_play_dev, s_volume);
    // audio_player 启动时会静音，PCM 直通路径必须显式解除，否则写入成功但无声
    esp_codec_dev_set_out_mute(s_play_dev, false);
    pa_en(1);
    return ESP_OK;
}

esp_err_t bsp_ai_audio_pcm_write(const void *data, size_t len)
{
    if (s_play_dev == NULL || !s_dev_open || data == NULL || len == 0) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_write(s_play_dev, (void *)data, len);
}

esp_err_t bsp_ai_audio_pcm_mute(bool mute)
{
    if (s_play_dev == NULL || !s_dev_open) {
        return ESP_ERR_INVALID_STATE;
    }
    return esp_codec_dev_set_out_mute(s_play_dev, mute);
}

esp_err_t bsp_ai_audio_pcm_close(void)
{
    pa_en(0);
    if (s_play_dev != NULL && s_dev_open) {
        (void)esp_codec_dev_close(s_play_dev);
        s_dev_open = false;
    }
    if (s_tx_chan != NULL) {
        (void)i2s_channel_enable(s_tx_chan);
    }
    return ESP_OK;
}
