// bsp_ai_record.c — ES7210 双麦录音并保存 WAV 到 SD 卡
#include "bsp_ai_record.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/stat.h>

#include "bsp_ai_audio.h"
#include "bsp_ai_sdcard.h"
#include "driver/i2s_std.h"
#include "esp_check.h"
#include "esp_codec_dev.h"
#include "esp_codec_dev_defaults.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "i2c_pca9557.h"

static const char *TAG = "bsp_ai_rec";

#define BSP_AI_REC_I2S_PORT     (0)
#define BSP_AI_REC_SAMPLE_RATE  (16000)
#define BSP_AI_REC_CHANNELS     (2)
#define BSP_AI_REC_BITS         (16)
#define BSP_AI_REC_GAIN_DB      (30.0f)
#define BSP_AI_REC_BUF_SAMPLES  (1024)
#define BSP_AI_REC_PATH_LEN     (300)
#define BSP_AI_REC_TASK_STACK   (4096)

#define BSP_AI_REC_MAX_FILES    (999)

typedef struct __attribute__((packed)) {
    char riff[4];
    uint32_t chunk_size;
    char wave[4];
    char fmt[4];
    uint32_t fmt_size;
    uint16_t audio_format;
    uint16_t channels;
    uint32_t sample_rate;
    uint32_t byte_rate;
    uint16_t block_align;
    uint16_t bits_per_sample;
    char data[4];
    uint32_t data_size;
} wav_header_t;

typedef char wav_header_size_check[(sizeof(wav_header_t) == 44) ? 1 : -1];

static volatile bsp_ai_rec_state_t s_state = BSP_AI_REC_IDLE;
static volatile bool s_paused = false;
static volatile bool s_stop_req = false;
static volatile bool s_cancel_req = false;
static volatile bool s_busy = false;
static volatile uint32_t s_data_bytes = 0;
static FILE *s_fp = NULL;
static char s_path[BSP_AI_REC_PATH_LEN];
static char s_last_path[BSP_AI_REC_PATH_LEN];

static esp_codec_dev_handle_t s_rec_dev = NULL;
static bool s_dev_created = false;

static wav_header_t make_header(uint32_t data_bytes)
{
    wav_header_t header = {0};
    const uint16_t block_align = BSP_AI_REC_CHANNELS * BSP_AI_REC_BITS / 8;

    memcpy(header.riff, "RIFF", 4);
    header.chunk_size = 36 + data_bytes;
    memcpy(header.wave, "WAVE", 4);
    memcpy(header.fmt, "fmt ", 4);
    header.fmt_size = 16;
    header.audio_format = 1;
    header.channels = BSP_AI_REC_CHANNELS;
    header.sample_rate = BSP_AI_REC_SAMPLE_RATE;
    header.byte_rate = BSP_AI_REC_SAMPLE_RATE * block_align;
    header.block_align = block_align;
    header.bits_per_sample = BSP_AI_REC_BITS;
    memcpy(header.data, "data", 4);
    header.data_size = data_bytes;
    return header;
}

static bool next_file_path(void)
{
    for (unsigned i = 1; i <= BSP_AI_REC_MAX_FILES; i++) {
        char candidate[BSP_AI_REC_PATH_LEN];
        snprintf(candidate, sizeof(candidate), "%s/录音_%03u.wav", BSP_AI_REC_DIR, i);
        struct stat st;
        if (stat(candidate, &st) != 0) {
            snprintf(s_path, sizeof(s_path), "%s", candidate);
            return true;
        }
    }
    return false;
}

static esp_err_t rec_dev_open(void)
{
    i2s_chan_handle_t rx = bsp_ai_audio_rx_handle();
    ESP_RETURN_ON_FALSE(rx != NULL, ESP_ERR_INVALID_STATE, TAG, "I2S rx not ready");

    if (!s_dev_created) {
        audio_codec_i2s_cfg_t i2s_cfg = {
            .port = BSP_AI_REC_I2S_PORT,
            .rx_handle = rx,
            .tx_handle = NULL,
        };
        const audio_codec_data_if_t *data_if = audio_codec_new_i2s_data(&i2s_cfg);
        ESP_RETURN_ON_FALSE(data_if != NULL, ESP_FAIL, TAG, "create record data if failed");

        audio_codec_i2c_cfg_t i2c_cfg = {
            .port = BSP_I2C_NUM,
            // ES7210 7 位地址 0x41，这里用 8 位写地址 0x82（左移 1 位）
            .addr = 0x82,
            .bus_handle = i2c_bus_handle,
        };
        const audio_codec_ctrl_if_t *ctrl_if = audio_codec_new_i2c_ctrl(&i2c_cfg);
        ESP_RETURN_ON_FALSE(ctrl_if != NULL, ESP_FAIL, TAG, "create record ctrl if failed");

        es7210_codec_cfg_t es7210_cfg = {
            .ctrl_if = ctrl_if,
            .master_mode = false,
            .mic_selected = ES7210_SEL_MIC1 | ES7210_SEL_MIC2,
            .mclk_src = ES7210_MCLK_FROM_PAD,
            .mclk_div = 256,
        };
        const audio_codec_if_t *es7210 = es7210_codec_new(&es7210_cfg);
        ESP_RETURN_ON_FALSE(es7210 != NULL, ESP_FAIL, TAG, "create ES7210 failed");

        esp_codec_dev_cfg_t dev_cfg = {
            .dev_type = ESP_CODEC_DEV_TYPE_IN,
            .codec_if = es7210,
            .data_if = data_if,
        };
        s_rec_dev = esp_codec_dev_new(&dev_cfg);
        ESP_RETURN_ON_FALSE(s_rec_dev != NULL, ESP_ERR_NO_MEM, TAG, "create record dev failed");
        s_dev_created = true;
    }

    (void)esp_codec_dev_close(s_rec_dev);
    esp_codec_dev_sample_info_t fs = {
        .sample_rate = BSP_AI_REC_SAMPLE_RATE,
        .channel = BSP_AI_REC_CHANNELS,
        .bits_per_sample = BSP_AI_REC_BITS,
    };
    ESP_RETURN_ON_ERROR(esp_codec_dev_open(s_rec_dev, &fs), TAG, "open record dev failed");
    esp_codec_dev_set_in_gain(s_rec_dev, BSP_AI_REC_GAIN_DB);
    // ES7210 上电后需要一段稳定时间，直接录音会把启动瞬态写进 WAV 开头
    vTaskDelay(pdMS_TO_TICKS(300));
    return ESP_OK;
}

esp_err_t bsp_ai_record_pcm_open(void)
{
    return rec_dev_open();
}

esp_err_t bsp_ai_record_pcm_read_timeout(void *buf, size_t len, size_t *got, uint32_t timeout_ms)
{
    i2s_chan_handle_t rx = bsp_ai_audio_rx_handle();
    if (rx == NULL || buf == NULL || got == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    return i2s_channel_read(rx, buf, len, got, pdMS_TO_TICKS(timeout_ms));
}

esp_err_t bsp_ai_record_pcm_read(void *buf, size_t len, size_t *got)
{
    return bsp_ai_record_pcm_read_timeout(buf, len, got, 500);
}

void bsp_ai_record_pcm_close(void)
{
    if (s_rec_dev != NULL) {
        (void)esp_codec_dev_close(s_rec_dev);
    }
}

static void record_task(void *arg)
{
    (void)arg;
    int16_t *buf = malloc(BSP_AI_REC_BUF_SAMPLES * sizeof(int16_t));
    const size_t buf_bytes = BSP_AI_REC_BUF_SAMPLES * sizeof(int16_t);

    if (buf == NULL) {
        ESP_LOGE(TAG, "alloc record buffer failed");
        s_cancel_req = true;
    }

    i2s_chan_handle_t rx = bsp_ai_audio_rx_handle();
    while (!s_stop_req && !s_cancel_req) {
        size_t bytes_read = 0;
        if (buf == NULL) {
            break;
        }
        esp_err_t err = i2s_channel_read(rx, buf, buf_bytes, &bytes_read, 100);
        if (err != ESP_OK || bytes_read == 0) {
            continue;
        }
        if (s_paused) {
            continue; // 暂停时保持 I2S 运行，丢弃数据
        }
        if (fwrite(buf, 1, bytes_read, s_fp) != bytes_read) {
            ESP_LOGE(TAG, "write wav failed, cancel recording");
            s_cancel_req = true;
            break;
        }
        s_data_bytes += bytes_read;
    }

    free(buf);

    if (s_fp != NULL) {
        if (s_cancel_req) {
            fclose(s_fp);
            remove(s_path);
            ESP_LOGW(TAG, "recording cancelled, file removed");
            s_state = BSP_AI_REC_IDLE;
        } else {
            wav_header_t header = make_header(s_data_bytes);
            if (fseek(s_fp, 0, SEEK_SET) == 0) {
                fwrite(&header, 1, sizeof(header), s_fp);
            }
            fclose(s_fp);
            snprintf(s_last_path, sizeof(s_last_path), "%s", s_path);
            s_state = BSP_AI_REC_DONE;
            ESP_LOGI(TAG, "recording saved: %s (%u bytes)", s_last_path, (unsigned)s_data_bytes);
        }
        s_fp = NULL;
    }

    if (s_rec_dev != NULL) {
        esp_codec_dev_close(s_rec_dev);
    }
    s_busy = false;
    vTaskDelete(NULL);
}

esp_err_t bsp_ai_record_start(void)
{
    if (s_state == BSP_AI_REC_RECORDING || s_state == BSP_AI_REC_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    if (!bsp_ai_sd_is_mounted()) {
        return ESP_ERR_INVALID_STATE;
    }
    (void)bsp_ai_sd_prepare_dirs();

    // 录音与播放共用 I2S 通道，必须先完全停止播放并关闭输出设备
    (void)bsp_ai_audio_stop();

    esp_err_t err = rec_dev_open();
    if (err != ESP_OK) {
        return err;
    }

    if (!next_file_path()) {
        esp_codec_dev_close(s_rec_dev);
        return ESP_ERR_NO_MEM;
    }

    s_fp = fopen(s_path, "wb");
    if (s_fp == NULL) {
        esp_codec_dev_close(s_rec_dev);
        ESP_LOGE(TAG, "open %s failed", s_path);
        return ESP_FAIL;
    }

    wav_header_t placeholder = make_header(0);
    fwrite(&placeholder, 1, sizeof(placeholder), s_fp);

    s_paused = false;
    s_stop_req = false;
    s_cancel_req = false;
    s_data_bytes = 0;
    s_busy = true;
    s_state = BSP_AI_REC_RECORDING;

    BaseType_t ret = xTaskCreatePinnedToCore(record_task, "bsp_ai_rec", BSP_AI_REC_TASK_STACK,
                                             NULL, 5, NULL, 1);
    if (ret != pdPASS) {
        s_busy = false;
        fclose(s_fp);
        s_fp = NULL;
        remove(s_path);
        esp_codec_dev_close(s_rec_dev);
        s_state = BSP_AI_REC_IDLE;
        return ESP_ERR_NO_MEM;
    }

    ESP_LOGI(TAG, "recording start: %s", s_path);
    return ESP_OK;
}

esp_err_t bsp_ai_record_pause(void)
{
    if (s_state != BSP_AI_REC_RECORDING) {
        return ESP_ERR_INVALID_STATE;
    }
    s_paused = true;
    s_state = BSP_AI_REC_PAUSED;
    return ESP_OK;
}

esp_err_t bsp_ai_record_resume(void)
{
    if (s_state != BSP_AI_REC_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_paused = false;
    s_state = BSP_AI_REC_RECORDING;
    return ESP_OK;
}

static void wait_task_done(void)
{
    uint32_t waited = 0;
    while (s_busy && waited < 3000) {
        vTaskDelay(pdMS_TO_TICKS(10));
        waited += 10;
    }
}

esp_err_t bsp_ai_record_stop(void)
{
    if (s_state != BSP_AI_REC_RECORDING && s_state != BSP_AI_REC_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_paused = false;
    s_stop_req = true;
    wait_task_done();
    return ESP_OK;
}

esp_err_t bsp_ai_record_cancel(void)
{
    if (s_state != BSP_AI_REC_RECORDING && s_state != BSP_AI_REC_PAUSED) {
        return ESP_ERR_INVALID_STATE;
    }
    s_paused = false;
    s_cancel_req = true;
    wait_task_done();
    return ESP_OK;
}

bsp_ai_rec_state_t bsp_ai_record_state(void)
{
    return s_state;
}

uint32_t bsp_ai_record_seconds(void)
{
    return s_data_bytes / (BSP_AI_REC_SAMPLE_RATE * BSP_AI_REC_CHANNELS * (BSP_AI_REC_BITS / 8));
}

const char *bsp_ai_record_last_path(void)
{
    return s_last_path;
}
