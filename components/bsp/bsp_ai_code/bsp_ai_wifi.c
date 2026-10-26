// bsp_ai_wifi.c — WiFi 扫描 / 连接 / 凭据保存与自动连接
#include "bsp_ai_wifi.h"

#include <dirent.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <strings.h>
#include <sys/stat.h>
#include <time.h>

#include "esp_check.h"
#include "esp_event.h"
#include "esp_log.h"
#include "esp_netif.h"
#include "esp_wifi.h"
#include "freertos/FreeRTOS.h"
#include "freertos/event_groups.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "nvs_flash.h"

static const char *TAG = "bsp_ai_wifi";

#define WIFI_CONNECTED_BIT      BIT0
#define WIFI_FAIL_BIT           BIT1
#define WIFI_STARTED_BIT        BIT2
#define WIFI_CONNECT_TIMEOUT_MS (15000)
#define WIFI_MAX_SAVED          (8)
#define WIFI_NAME_LEN           (64)
#define WIFI_PATH_LEN           (300)

typedef struct {
    char ssid[BSP_AI_WIFI_SSID_MAX + 1];
    char password[BSP_AI_WIFI_PASS_MAX + 1];
} wifi_request_t;

typedef struct {
    char ssid[BSP_AI_WIFI_SSID_MAX + 1];
    int rssi;
    bool secure;
} wifi_ap_t;

typedef struct {
    char name[WIFI_NAME_LEN];
    time_t mtime;
} saved_net_t;

static bool s_inited = false;
static volatile bool s_connecting = false;
static volatile bool s_scan_busy = false;
static volatile bool s_scan_done = false;
static bool s_connected = false;
static char s_current_ssid[BSP_AI_WIFI_SSID_MAX + 1];
static char s_pending_ssid[BSP_AI_WIFI_SSID_MAX + 1];

static SemaphoreHandle_t s_lock = NULL;
static EventGroupHandle_t s_events = NULL;
static QueueHandle_t s_requests = NULL;
static TaskHandle_t s_scan_task = NULL;
static TaskHandle_t s_connect_task = NULL;
static TaskHandle_t s_auto_task = NULL;

static wifi_ap_t s_aps[BSP_AI_WIFI_MAX_AP];
static size_t s_ap_count = 0;

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

static void set_connected(bool connected, const char *ssid)
{
    if (s_lock != NULL) {
        xSemaphoreTake(s_lock, portMAX_DELAY);
    }
    s_connected = connected;
    if (connected && ssid != NULL) {
        copy_str(s_current_ssid, sizeof(s_current_ssid), ssid);
    } else if (!connected) {
        s_current_ssid[0] = '\0';
    }
    if (s_lock != NULL) {
        xSemaphoreGive(s_lock);
    }
}

static void wifi_event_handler(void *arg, esp_event_base_t base, int32_t id, void *data)
{
    (void)arg;
    if (base == WIFI_EVENT && id == WIFI_EVENT_STA_START) {
        xEventGroupSetBits(s_events, WIFI_STARTED_BIT);
    } else if (base == WIFI_EVENT && id == WIFI_EVENT_STA_DISCONNECTED) {
        const wifi_event_sta_disconnected_t *ev = (const wifi_event_sta_disconnected_t *)data;
        ESP_LOGW(TAG, "disconnected, reason=%d", ev->reason);
        set_connected(false, NULL);
        xEventGroupSetBits(s_events, WIFI_FAIL_BIT);
    } else if (base == IP_EVENT && id == IP_EVENT_STA_GOT_IP) {
        set_connected(true, s_pending_ssid);
        xEventGroupSetBits(s_events, WIFI_CONNECTED_BIT);
    }
}

static esp_err_t nvs_init(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

// ---------------- 扫描 ----------------

static int ap_cmp(const void *a, const void *b)
{
    const wifi_ap_t *aa = (const wifi_ap_t *)a;
    const wifi_ap_t *ab = (const wifi_ap_t *)b;
    return ab->rssi - aa->rssi;
}

static void add_ap(const wifi_ap_record_t *rec)
{
    if (rec->ssid[0] == '\0') {
        return;
    }
    const char *ssid = (const char *)rec->ssid;
    for (size_t i = 0; i < s_ap_count; i++) {
        if (strcmp(s_aps[i].ssid, ssid) == 0) {
            if (rec->rssi > s_aps[i].rssi) {
                s_aps[i].rssi = rec->rssi;
            }
            return;
        }
    }
    if (s_ap_count >= BSP_AI_WIFI_MAX_AP) {
        return;
    }
    wifi_ap_t *ap = &s_aps[s_ap_count++];
    copy_str(ap->ssid, sizeof(ap->ssid), ssid);
    ap->rssi = rec->rssi;
    ap->secure = (rec->authmode != WIFI_AUTH_OPEN);
}

static void do_scan(void)
{
    static wifi_ap_record_t records[32];
    wifi_scan_config_t cfg = {
        .scan_type = WIFI_SCAN_TYPE_ACTIVE,
    };

    s_scan_busy = true;
    s_scan_done = false;
    s_ap_count = 0;

    esp_err_t err = esp_wifi_scan_start(&cfg, true);
    if (err == ESP_OK) {
        uint16_t num = 0;
        esp_wifi_scan_get_ap_num(&num);
        uint16_t want = (num > 32) ? 32 : num;
        if (want > 0 && esp_wifi_scan_get_ap_records(&want, records) == ESP_OK) {
            for (uint16_t i = 0; i < want; i++) {
                add_ap(&records[i]);
            }
        }
        qsort(s_aps, s_ap_count, sizeof(s_aps[0]), ap_cmp);
    } else {
        ESP_LOGW(TAG, "scan failed: %s", esp_err_to_name(err));
    }

    s_scan_busy = false;
    s_scan_done = true;
    ESP_LOGI(TAG, "scan done, %u AP", (unsigned)s_ap_count);
}

static void scan_task(void *arg)
{
    (void)arg;
    for (;;) {
        ulTaskNotifyTake(pdTRUE, portMAX_DELAY);
        do_scan();
    }
}

// ---------------- 连接 ----------------

static esp_err_t apply_sta_config(const char *ssid, const char *password)
{
    wifi_config_t cfg;
    memset(&cfg, 0, sizeof(cfg));
    size_t n = strnlen(ssid, BSP_AI_WIFI_SSID_MAX);
    memcpy(cfg.sta.ssid, ssid, n);
    n = strnlen(password, BSP_AI_WIFI_PASS_MAX);
    memcpy(cfg.sta.password, password, n);
    cfg.sta.threshold.authmode = (password[0] != '\0') ? WIFI_AUTH_WPA2_PSK : WIFI_AUTH_OPEN;
    cfg.sta.pmf_cfg.capable = true;
    cfg.sta.pmf_cfg.required = false;
    return esp_wifi_set_config(WIFI_IF_STA, &cfg);
}

static esp_err_t save_network(const char *ssid, const char *password);

static void do_connect(const char *ssid, const char *password)
{
    s_connecting = true;

    if (s_connected && strcmp(s_current_ssid, ssid) == 0) {
        s_connecting = false;
        return;
    }

    (void)esp_wifi_disconnect();
    vTaskDelay(pdMS_TO_TICKS(200));

    if (apply_sta_config(ssid, password) != ESP_OK) {
        ESP_LOGE(TAG, "apply config failed");
        s_connecting = false;
        return;
    }

    copy_str(s_pending_ssid, sizeof(s_pending_ssid), ssid);
    xEventGroupClearBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT);

    esp_err_t err = esp_wifi_connect();
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "connect %s failed: %s", ssid, esp_err_to_name(err));
        s_connecting = false;
        return;
    }

    EventBits_t bits = xEventGroupWaitBits(s_events, WIFI_CONNECTED_BIT | WIFI_FAIL_BIT,
                                           pdFALSE, pdFALSE,
                                           pdMS_TO_TICKS(WIFI_CONNECT_TIMEOUT_MS));
    if ((bits & WIFI_CONNECTED_BIT) != 0) {
        ESP_LOGI(TAG, "connected to %s", ssid);
        if (save_network(ssid, password) != ESP_OK) {
            ESP_LOGW(TAG, "save credentials failed");
        }
    } else {
        ESP_LOGW(TAG, "connect %s timeout/fail", ssid);
        (void)esp_wifi_disconnect();
    }

    s_connecting = false;
}

static void connect_task(void *arg)
{
    (void)arg;
    wifi_request_t req;
    for (;;) {
        if (xQueueReceive(s_requests, &req, portMAX_DELAY) == pdTRUE) {
            do_connect(req.ssid, req.password);
        }
    }
}

// ---------------- 凭据文件 ----------------

static void sanitize_name(const char *ssid, char *out, size_t out_len)
{
    size_t j = 0;
    for (size_t i = 0; ssid[i] != '\0' && j + 1 < out_len; i++) {
        unsigned char c = (unsigned char)ssid[i];
        if (c < 0x20 || strchr("/\\:*?\"<>|", (int)c) != NULL) {
            c = '_';
        }
        out[j++] = (char)c;
    }
    while (j > 0 && (out[j - 1] == ' ' || out[j - 1] == '.')) {
        j--;
    }
    out[j] = '\0';
    if (j == 0) {
        copy_str(out, out_len, "wifi");
    }
    if (out[0] == '.') {
        out[0] = '_';
    }
}

static bool same_network(const char *path, const char *ssid)
{
    char saved_ssid[BSP_AI_WIFI_SSID_MAX + 1] = {0};
    char saved_pass[BSP_AI_WIFI_PASS_MAX + 1] = {0};
    if (bsp_ai_wifi_read_file(path, saved_ssid, sizeof(saved_ssid), saved_pass,
                              sizeof(saved_pass)) != ESP_OK) {
        return false;
    }
    return strcmp(saved_ssid, ssid) == 0;
}

static esp_err_t save_network(const char *ssid, const char *password)
{
    if (bsp_ai_sd_prepare_dirs() != ESP_OK) {
        return ESP_ERR_INVALID_STATE;
    }

    char base[WIFI_NAME_LEN];
    char path[WIFI_PATH_LEN] = {0};
    sanitize_name(ssid, base, sizeof(base));

    for (int attempt = 0; attempt < 20; attempt++) {
        char candidate[WIFI_PATH_LEN];
        if (attempt == 0) {
            snprintf(candidate, sizeof(candidate), "%s/%s.txt", BSP_AI_WIFI_DIR, base);
        } else {
            snprintf(candidate, sizeof(candidate), "%s/%s_%d.txt", BSP_AI_WIFI_DIR, base,
                     attempt + 1);
        }
        struct stat st;
        if (stat(candidate, &st) != 0 || same_network(candidate, ssid)) {
            copy_str(path, sizeof(path), candidate);
            break;
        }
    }
    if (path[0] == '\0') {
        return ESP_FAIL;
    }

    FILE *fp = fopen(path, "w");
    if (fp == NULL) {
        ESP_LOGE(TAG, "open %s for write failed", path);
        return ESP_FAIL;
    }
    fprintf(fp, "SSID=%s\n", ssid);
    fprintf(fp, "PASSWORD=%s\n", password);
    fclose(fp);
    ESP_LOGI(TAG, "saved %s", path);
    return ESP_OK;
}

esp_err_t bsp_ai_wifi_read_file(const char *path, char *ssid, size_t ssid_len,
                                char *password, size_t password_len)
{
    if (path == NULL || ssid == NULL || password == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    ssid[0] = '\0';
    password[0] = '\0';

    FILE *fp = fopen(path, "r");
    if (fp == NULL) {
        return ESP_ERR_NOT_FOUND;
    }

    char line[160];
    while (fgets(line, sizeof(line), fp) != NULL) {
        size_t len = strlen(line);
        while (len > 0 && (line[len - 1] == '\n' || line[len - 1] == '\r')) {
            line[--len] = '\0';
        }
        if (strncmp(line, "SSID=", 5) == 0) {
            copy_str(ssid, ssid_len, line + 5);
        } else if (strncmp(line, "PASSWORD=", 9) == 0) {
            copy_str(password, password_len, line + 9);
        }
    }
    fclose(fp);
    return (ssid[0] != '\0') ? ESP_OK : ESP_ERR_INVALID_STATE;
}

// ---------------- 自动连接 ----------------

static int saved_cmp(const void *a, const void *b)
{
    const saved_net_t *na = (const saved_net_t *)a;
    const saved_net_t *nb = (const saved_net_t *)b;
    if (na->mtime < nb->mtime) {
        return 1;
    }
    if (na->mtime > nb->mtime) {
        return -1;
    }
    return 0;
}

static size_t list_saved(saved_net_t *out, size_t max)
{
    DIR *dp = opendir(BSP_AI_WIFI_DIR);
    if (dp == NULL) {
        return 0;
    }

    size_t count = 0;
    struct dirent *ent = NULL;
    while ((ent = readdir(dp)) != NULL && count < max) {
        const char *dot = strrchr(ent->d_name, '.');
        if (dot == NULL || strcasecmp(dot, ".txt") != 0) {
            continue;
        }
        char path[WIFI_PATH_LEN];
        snprintf(path, sizeof(path), "%s/%s", BSP_AI_WIFI_DIR, ent->d_name);
        struct stat st;
        if (stat(path, &st) != 0 || S_ISDIR(st.st_mode)) {
            continue;
        }
        copy_str(out[count].name, sizeof(out[count].name), ent->d_name);
        out[count].mtime = st.st_mtime;
        count++;
    }
    closedir(dp);
    qsort(out, count, sizeof(out[0]), saved_cmp);
    return count;
}

static void auto_connect_task(void *arg)
{
    (void)arg;
    vTaskDelay(pdMS_TO_TICKS(2500));
    if (s_connected) {
        return;
    }

    saved_net_t saved[WIFI_MAX_SAVED];
    size_t count = list_saved(saved, WIFI_MAX_SAVED);
    if (count == 0) {
        ESP_LOGI(TAG, "no saved WiFi network");
        vTaskDelete(NULL);
        return;
    }

    for (size_t i = 0; i < count && !s_connected; i++) {
        char path[WIFI_PATH_LEN];
        wifi_request_t req;
        snprintf(path, sizeof(path), "%s/%s", BSP_AI_WIFI_DIR, saved[i].name);
        if (bsp_ai_wifi_read_file(path, req.ssid, sizeof(req.ssid), req.password,
                                  sizeof(req.password)) != ESP_OK) {
            continue;
        }
        ESP_LOGI(TAG, "auto connect try %s", req.ssid);
        if (xQueueSend(s_requests, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
            continue;
        }
        vTaskDelay(pdMS_TO_TICKS(300));
        while (s_connecting) {
            vTaskDelay(pdMS_TO_TICKS(100));
        }
    }
    vTaskDelete(NULL);
}

// ---------------- 对外接口 ----------------

esp_err_t bsp_ai_wifi_init(void)
{
    if (s_inited) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(nvs_init(), TAG, "nvs init failed");
    ESP_RETURN_ON_ERROR(esp_netif_init(), TAG, "netif init failed");
    esp_err_t err = esp_event_loop_create_default();
    if (err != ESP_OK && err != ESP_ERR_INVALID_STATE) {
        return err;
    }
    if (esp_netif_create_default_wifi_sta() == NULL) {
        return ESP_FAIL;
    }

    s_lock = xSemaphoreCreateMutex();
    s_events = xEventGroupCreate();
    s_requests = xQueueCreate(4, sizeof(wifi_request_t));
    if (s_lock == NULL || s_events == NULL || s_requests == NULL) {
        return ESP_ERR_NO_MEM;
    }

    wifi_init_config_t init_cfg = WIFI_INIT_CONFIG_DEFAULT();
    ESP_RETURN_ON_ERROR(esp_wifi_init(&init_cfg), TAG, "wifi init failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(WIFI_EVENT, ESP_EVENT_ANY_ID,
                                                            wifi_event_handler, NULL, NULL),
                        TAG, "register wifi event failed");
    ESP_RETURN_ON_ERROR(esp_event_handler_instance_register(IP_EVENT, IP_EVENT_STA_GOT_IP,
                                                            wifi_event_handler, NULL, NULL),
                        TAG, "register ip event failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_storage(WIFI_STORAGE_RAM), TAG, "set storage failed");
    ESP_RETURN_ON_ERROR(esp_wifi_set_mode(WIFI_MODE_STA), TAG, "set mode failed");
    ESP_RETURN_ON_ERROR(esp_wifi_start(), TAG, "wifi start failed");

    if (xTaskCreatePinnedToCore(scan_task, "wifi_scan", 4096, NULL, 3, &s_scan_task, 0) != pdPASS ||
        xTaskCreatePinnedToCore(connect_task, "wifi_conn", 4096, NULL, 4, &s_connect_task, 0) != pdPASS) {
        return ESP_ERR_NO_MEM;
    }

    s_scan_done = false;
    s_inited = true;
    ESP_LOGI(TAG, "wifi ready");
    return ESP_OK;
}

esp_err_t bsp_ai_wifi_auto_connect_start(void)
{
    if (!s_inited) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_auto_task != NULL) {
        return ESP_OK;
    }
    if (xTaskCreatePinnedToCore(auto_connect_task, "wifi_auto", 4096, NULL, 2, &s_auto_task, 0) !=
        pdPASS) {
        return ESP_ERR_NO_MEM;
    }
    return ESP_OK;
}

esp_err_t bsp_ai_wifi_scan_start(void)
{
    if (!s_inited || s_scan_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_scan_busy) {
        return ESP_ERR_INVALID_STATE;
    }
    s_scan_busy = true;
    s_scan_done = false;
    s_ap_count = 0;
    xTaskNotifyGive(s_scan_task);
    return ESP_OK;
}

bool bsp_ai_wifi_scan_busy(void)
{
    return s_scan_busy;
}

bool bsp_ai_wifi_scan_done(void)
{
    return s_scan_done;
}

size_t bsp_ai_wifi_ap_count(void)
{
    return s_ap_count;
}

const char *bsp_ai_wifi_ap_ssid(size_t index)
{
    if (index >= s_ap_count) {
        return NULL;
    }
    return s_aps[index].ssid;
}

int bsp_ai_wifi_ap_rssi(size_t index)
{
    if (index >= s_ap_count) {
        return 0;
    }
    return s_aps[index].rssi;
}

bool bsp_ai_wifi_ap_secure(size_t index)
{
    if (index >= s_ap_count) {
        return false;
    }
    return s_aps[index].secure;
}

bool bsp_ai_wifi_connected(void)
{
    return s_connected;
}

bool bsp_ai_wifi_connecting(void)
{
    return s_connecting;
}

const char *bsp_ai_wifi_current_ssid(void)
{
    return s_current_ssid;
}

esp_err_t bsp_ai_wifi_connect_async(const char *ssid, const char *password)
{
    if (!s_inited || ssid == NULL || ssid[0] == '\0') {
        return ESP_ERR_INVALID_ARG;
    }
    if (strnlen(ssid, BSP_AI_WIFI_SSID_MAX + 1) > BSP_AI_WIFI_SSID_MAX) {
        return ESP_ERR_INVALID_ARG;
    }
    if (password == NULL) {
        password = "";
    }
    if (strnlen(password, BSP_AI_WIFI_PASS_MAX + 1) > BSP_AI_WIFI_PASS_MAX) {
        return ESP_ERR_INVALID_ARG;
    }

    wifi_request_t req;
    copy_str(req.ssid, sizeof(req.ssid), ssid);
    copy_str(req.password, sizeof(req.password), password);
    if (xQueueSend(s_requests, &req, pdMS_TO_TICKS(100)) != pdTRUE) {
        return ESP_ERR_TIMEOUT;
    }
    return ESP_OK;
}
