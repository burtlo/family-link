#include "lab02/page_provider_http.h"

#include "lab02/card_pool.h"
#include "lab02/instrumentation.h"
#include "lab02/layout.h"
#include "lab02/page_provider.h"
#include "lab02/trace_emit.h"
#include "lab02_platform/wifi.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdio.h>
#include <string.h>

static const char *TAG = "lab02_http";

#define HTTP_BUF_SIZE 16384
#define MAX_PAGES 32

static lab02_inbox_model_t s_model;
static lab02_page_provider_t *s_pp;
static bool s_active;
static uint32_t s_loaded_pages;
static uint32_t s_inbox_revision;

static char s_pending_json[HTTP_BUF_SIZE];
static int s_pending_offset = -1;
static volatile bool s_pending_ready;
static SemaphoreHandle_t s_fetch_sem;
static int s_requested_page = -1;
static TaskHandle_t s_worker;

static int page_for_index(int focus, uint16_t page_size)
{
    if (page_size == 0) {
        page_size = LAB02_DEFAULT_PAGE_SIZE;
    }
    return focus / (int)page_size;
}

static bool http_fetch_page(int page_offset, char *buf, size_t buf_len, size_t *out_len)
{
    if (strlen(CONFIG_LAB02_SERVER_URL) == 0) {
        return false;
    }
    char url[192];
    snprintf(url, sizeof(url), "%s/v2/inbox?cursor=%d&limit=%d", CONFIG_LAB02_SERVER_URL,
             page_offset, (int)LAB02_DEFAULT_PAGE_SIZE);
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 10000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return false;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        esp_http_client_cleanup(client);
        return false;
    }
    int total = esp_http_client_fetch_headers(client);
    if (total < 0) {
        total = 0;
    }
    size_t written = 0;
    while (written < buf_len - 1) {
        int r = esp_http_client_read(client, buf + written, (int)(buf_len - 1 - written));
        if (r <= 0) {
            break;
        }
        written += (size_t)r;
    }
    buf[written] = 0;
    *out_len = written;
    esp_http_client_cleanup(client);
    lab02_trace_emit("page_fetch", 0, page_offset, NULL, NULL);
    return written > 0;
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        if (xSemaphoreTake(s_fetch_sem, portMAX_DELAY) != pdTRUE) {
            continue;
        }
        int page = s_requested_page;
        if (page < 0) {
            continue;
        }
        int offset = page * (int)LAB02_DEFAULT_PAGE_SIZE;
        char tmp[HTTP_BUF_SIZE];
        size_t len = 0;
        if (!http_fetch_page(offset, tmp, sizeof(tmp), &len)) {
            ESP_LOGW(TAG, "fetch page %d failed", page);
            continue;
        }
        if (len >= sizeof(s_pending_json)) {
            ESP_LOGW(TAG, "page %d response too large", page);
            continue;
        }
        memcpy(s_pending_json, tmp, len + 1);
        s_pending_offset = offset;
        s_pending_ready = true;
    }
}

static void request_page_async(int page)
{
    if (page < 0 || page >= MAX_PAGES) {
        return;
    }
    if (s_loaded_pages & (1U << page)) {
        return;
    }
    s_requested_page = page;
    xSemaphoreGive(s_fetch_sem);
}

bool lab02_page_provider_http_active(void)
{
    return s_active;
}

bool lab02_page_provider_http_init(lab02_page_provider_t *pp)
{
    if (!pp || strlen(CONFIG_LAB02_SERVER_URL) == 0) {
        return false;
    }
    memset(pp, 0, sizeof(*pp));
    memset(&s_model, 0, sizeof(s_model));
    s_model.page_size = LAB02_DEFAULT_PAGE_SIZE;
    s_pp = pp;
    pp->model = &s_model;

    if (!lab02_board_wifi_connect()) {
        ESP_LOGE(TAG, "Wi-Fi failed");
        return false;
    }

    char buf[HTTP_BUF_SIZE];
    size_t len = 0;
    if (!http_fetch_page(0, buf, sizeof(buf), &len)) {
        ESP_LOGE(TAG, "initial page fetch failed");
        return false;
    }
    uint32_t rev = 0;
    if (!lab02_inbox_model_merge_v2_page(&s_model, 0, buf, len, &rev)) {
        ESP_LOGE(TAG, "parse initial page failed");
        return false;
    }
    s_inbox_revision = rev;
    s_loaded_pages = 1U;
    lab02_page_metadata_build(pp->model, &pp->meta);
    s_active = true;
    s_fetch_sem = xSemaphoreCreateBinary();
    if (!s_worker) {
        xTaskCreate(worker_task, "lab02_http", 8192, NULL, 4, &s_worker);
    }
    printf("LAB02_HTTP_READY revision=%u items=%u url=%s\n", (unsigned)s_inbox_revision,
           (unsigned)s_model.count, CONFIG_LAB02_SERVER_URL);
    return true;
}

void lab02_page_provider_http_start(void)
{
    request_page_async(1);
}

void lab02_page_provider_http_prefetch_for_focus(int focus_index)
{
    if (!s_active || !s_pp) {
        return;
    }
    int page = page_for_index(focus_index, s_pp->meta.page_size);
    request_page_async(page);
    request_page_async(page + 1);
}

bool lab02_page_provider_http_apply_if_idle(lab02_carousel_t *c)
{
    if (!s_active || !c || !s_pending_ready) {
        return false;
    }
    if (c->snap.scroll_lock) {
        return false;
    }
    s_pending_ready = false;
    uint32_t rev = 0;
    size_t len = strlen(s_pending_json);
    if (!lab02_inbox_model_merge_v2_page(&s_model, s_pending_offset, s_pending_json, len, &rev)) {
        ESP_LOGW(TAG, "pending merge failed");
        return false;
    }
    s_inbox_revision = rev;
    int page = s_pending_offset / (int)LAB02_DEFAULT_PAGE_SIZE;
    if (page >= 0 && page < MAX_PAGES) {
        s_loaded_pages |= (1U << page);
    }
    lab02_page_metadata_build(c->provider.model, &c->provider.meta);
    int old_count = c->item_count;
    c->item_count = (int)c->provider.model->count;
    if (c->focus_index >= c->item_count && c->item_count > 0) {
        c->focus_index = c->item_count - 1;
    }
    lab02_trace_emit("page_apply", 0, c->focus_index, NULL, NULL);
    if (c->content_spacer) {
        int32_t w = LAB02_CARD_GAP + c->item_count * (LAB02_CARD_W + LAB02_CARD_GAP);
        lv_obj_set_size(c->content_spacer, w, LAB02_CARD_H);
    }
    uint32_t gen = lab02_instrumentation_next_generation();
    lab02_card_pool_rebind(&c->pool, &c->provider, c->focus_index, gen);
    return c->item_count != old_count;
}
