// bsp_ai_sdcard.c — SD 卡挂载（1 线 SDMMC，引脚与 esp32s3_core 一致）
#include "bsp_ai_sdcard.h"

#include <errno.h>
#include <sys/stat.h>

#include "driver/sdmmc_host.h"
#include "esp_log.h"
#include "esp_vfs_fat.h"
#include "sdmmc_cmd.h"

static const char *TAG = "bsp_ai_sd";

static sdmmc_card_t *s_card = NULL;
static bool s_mounted = false;

static const char *const s_dirs[] = {
    BSP_AI_MUSIC_DIR,
    BSP_AI_REC_DIR,
    BSP_AI_RGB_DIR,
    BSP_AI_WIFI_DIR,
    BSP_AI_AI_DIR,
};

static void create_dirs(void)
{
    for (size_t i = 0; i < sizeof(s_dirs) / sizeof(s_dirs[0]); i++) {
        if (mkdir(s_dirs[i], 0777) == 0) {
            ESP_LOGI(TAG, "created dir %s", s_dirs[i]);
        } else if (errno != EEXIST) {
            ESP_LOGW(TAG, "create dir %s failed: errno=%d", s_dirs[i], errno);
        }
    }
}

esp_err_t bsp_ai_sd_prepare_dirs(void)
{
    if (!s_mounted) {
        return ESP_ERR_INVALID_STATE;
    }
    create_dirs();
    return ESP_OK;
}

esp_err_t bsp_ai_sd_mount(void)
{
    if (s_mounted) {
        return ESP_OK;
    }

    esp_vfs_fat_sdmmc_mount_config_t mount_config = {
        .format_if_mount_failed = false, // 不要因为挂载失败就格式化用户的卡
        .max_files = 8,
        .allocation_unit_size = 16 * 1024,
    };

    sdmmc_host_t host = SDMMC_HOST_DEFAULT();
    sdmmc_slot_config_t slot_config = SDMMC_SLOT_CONFIG_DEFAULT();
    slot_config.width = 1;
    slot_config.clk = BSP_AI_SD_CLK;
    slot_config.cmd = BSP_AI_SD_CMD;
    slot_config.d0 = BSP_AI_SD_D0;
    slot_config.flags |= SDMMC_SLOT_FLAG_INTERNAL_PULLUP;

    esp_err_t err = esp_vfs_fat_sdmmc_mount(BSP_AI_SD_MOUNT_POINT, &host, &slot_config,
                                            &mount_config, &s_card);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "mount %s failed: %s", BSP_AI_SD_MOUNT_POINT, esp_err_to_name(err));
        s_card = NULL;
        return err;
    }

    s_mounted = true;
    create_dirs();
    ESP_LOGI(TAG, "SD card mounted at %s", BSP_AI_SD_MOUNT_POINT);
    return ESP_OK;
}

bool bsp_ai_sd_is_mounted(void)
{
    return s_mounted;
}
