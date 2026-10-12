#include "lab02/page_provider_http.h"

#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02/trace_emit.h"
#include "lab02_platform/wifi.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <errno.h>
#include <stdio.h>
#include <string.h>

static const char *TAG = "lab02_http";

typedef enum {
    LAB02_HTTP_FETCH_OK,
    LAB02_HTTP_FETCH_FAIL,
    LAB02_HTTP_FETCH_TIMEOUT,
} lab02_http_fetch_result_t;

#define HTTP_BUF_SIZE LAB02_PAGE_JSON_MAX

static bool s_io_started;
static char s_fetch_scratch[HTTP_BUF_SIZE];
static lab02_inbox_page_t s_result_page;
static TaskHandle_t s_worker;
static TaskHandle_t s_boot_worker;
static uint32_t s_boot_session_id;
static bool ensure_workers(void);

static void on_wifi_transport(bool reachable)
{
    lab02_session_intent_transport(reachable);
}

static int page_offset_for_index(int page)
{
    return page * (int)lab02_session_http_page_size();
}

static bool http_errno_timed_out(int eno)
{
    return eno == ETIMEDOUT || eno == EWOULDBLOCK;
}

static lab02_http_fetch_result_t http_fetch_page(int page_offset, char *buf, size_t buf_len,
                                               size_t *out_len)
{
    if (strlen(CONFIG_LAB02_SERVER_URL) == 0) {
        return LAB02_HTTP_FETCH_FAIL;
    }
    char url[192];
    int url_len = snprintf(url, sizeof(url), "%s/v2/inbox?cursor=%d&limit=%d",
                           CONFIG_LAB02_SERVER_URL, page_offset,
                           (int)lab02_session_http_page_size());
    if (url_len < 0 || url_len >= (int)sizeof(url)) {
        return LAB02_HTTP_FETCH_FAIL;
    }
    esp_http_client_config_t cfg = {
        .url = url,
        .timeout_ms = 3000,
    };
    esp_http_client_handle_t client = esp_http_client_init(&cfg);
    if (!client) {
        return LAB02_HTTP_FETCH_FAIL;
    }
    esp_err_t err = esp_http_client_open(client, 0);
    if (err != ESP_OK) {
        int eno = esp_http_client_get_errno(client);
        esp_http_client_cleanup(client);
        if (http_errno_timed_out(eno) || err == ESP_ERR_HTTP_CONNECT) {
            return LAB02_HTTP_FETCH_TIMEOUT;
        }
        return LAB02_HTTP_FETCH_FAIL;
    }
    int total = esp_http_client_fetch_headers(client);
    if (total < 0) {
        int eno = esp_http_client_get_errno(client);
        esp_http_client_cleanup(client);
        if (http_errno_timed_out(eno)) {
            return LAB02_HTTP_FETCH_TIMEOUT;
        }
        return LAB02_HTTP_FETCH_FAIL;
    }
    int status = esp_http_client_get_status_code(client);
    if (status != 200 || total >= (int)buf_len) {
        esp_http_client_cleanup(client);
        return LAB02_HTTP_FETCH_FAIL;
    }
    size_t written = 0;
    bool read_error = false;
    bool deadline_expired = false;
    int64_t deadline_us = esp_timer_get_time() + 12000000;
    while (written < buf_len - 1) {
        if (esp_timer_get_time() >= deadline_us) {
            deadline_expired = true;
            break;
        }
        int r = esp_http_client_read(client, buf + written, (int)(buf_len - 1 - written));
        if (r < 0) {
            read_error = true;
            break;
        }
        if (r == 0) {
            break;
        }
        written += (size_t)r;
    }
    bool full = written == buf_len - 1;
    buf[written] = 0;
    *out_len = written;
    int eno = esp_http_client_get_errno(client);
    esp_http_client_cleanup(client);
    if (deadline_expired || http_errno_timed_out(eno)) {
        return LAB02_HTTP_FETCH_TIMEOUT;
    }
    if (read_error || full || (total > 0 && written != (size_t)total)) {
        return http_errno_timed_out(eno) ? LAB02_HTTP_FETCH_TIMEOUT : LAB02_HTTP_FETCH_FAIL;
    }
    if (written > 0) {
        lab02_trace_emit("page_fetch", 0, page_offset, NULL, NULL);
        return LAB02_HTTP_FETCH_OK;
    }
    if (http_errno_timed_out(eno)) {
        return LAB02_HTTP_FETCH_TIMEOUT;
    }
    return LAB02_HTTP_FETCH_FAIL;
}

static void worker_task(void *arg)
{
    (void)arg;
    for (;;) {
        lab02_fetch_req_t req;
        if (!lab02_session_http_take_fetch_request(&req)) {
            continue;
        }
        int offset = page_offset_for_index(req.page_index);
        size_t len = 0;
        lab02_http_fetch_result_t res =
            http_fetch_page(offset, s_fetch_scratch, sizeof(s_fetch_scratch), &len);
        if (res == LAB02_HTTP_FETCH_OK &&
            !lab02_inbox_model_parse_v2_page(s_fetch_scratch, len, offset, &s_result_page)) {
            res = LAB02_HTTP_FETCH_FAIL;
        }
        if (res == LAB02_HTTP_FETCH_OK) {
            lab02_session_http_fetch_finished(LAB02_FETCH_OK, req.session_id, req.op_id,
                                              req.page_index,
                                              &s_result_page, false);
        } else if (res == LAB02_HTTP_FETCH_TIMEOUT) {
            lab02_session_http_fetch_finished(LAB02_FETCH_TIMEOUT, req.session_id, req.op_id,
                                              req.page_index,
                                              NULL, false);
        } else {
            lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, req.session_id, req.op_id,
                                              req.page_index,
                                              NULL, false);
        }
    }
}

static void boot_task(void *arg)
{
    (void)arg;
    uint32_t session_id = s_boot_session_id;
    lab02_board_wifi_set_transport_notifier(on_wifi_transport);
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session_id);
    if (!lab02_board_wifi_connect()) {
        ESP_LOGE(TAG, "Wi-Fi failed");
        lab02_session_http_mark_stage(LAB02_LOAD_ERROR, session_id);
        vTaskDelete(NULL);
        return;
    }
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session_id);
    size_t len = 0;
    lab02_http_fetch_result_t boot_res =
        http_fetch_page(0, s_fetch_scratch, sizeof(s_fetch_scratch), &len);
    if (boot_res == LAB02_HTTP_FETCH_TIMEOUT) {
        ESP_LOGE(TAG, "initial page fetch timed out");
        lab02_trace_emit("fetch_timeout", 0, 0, NULL, "boot");
        lab02_session_http_fetch_finished(LAB02_FETCH_TIMEOUT, session_id, 0, 0, NULL, true);
        vTaskDelete(NULL);
        return;
    }
    if (boot_res != LAB02_HTTP_FETCH_OK) {
        ESP_LOGE(TAG, "initial page fetch failed");
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session_id, 0, 0, NULL, true);
        vTaskDelete(NULL);
        return;
    }
    if (!lab02_inbox_model_parse_v2_page(s_fetch_scratch, len, 0, &s_result_page)) {
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session_id, 0, 0, NULL, true);
        vTaskDelete(NULL);
        return;
    }
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, session_id, 0, 0, &s_result_page, true);
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    if (session_id == lab02_session_generation() && snap.load_stage == LAB02_LOAD_READY) {
        if (ensure_workers()) {
            printf("LAB02_HTTP_READY revision=%u items=%u\n", (unsigned)snap.inbox_revision,
                   (unsigned)snap.total_count);
        } else {
            ESP_LOGW(TAG, "background fetch worker unavailable; page 0 only");
            lab02_trace_emit("worker_spawn_fail", 0, 0, NULL, NULL);
        }
    }
    vTaskDelete(NULL);
}

static bool ensure_workers(void)
{
    if (!s_worker) {
        if (xTaskCreate(worker_task, "lab02_http", 8192, NULL, 4, &s_worker) != pdPASS) {
            return false;
        }
    }
    return true;
}

bool lab02_page_provider_http_init(lab02_page_provider_t *pp)
{
    if (!pp || strlen(CONFIG_LAB02_SERVER_URL) == 0) {
        return false;
    }
    if (!lab02_session_begin_http(pp)) {
        return false;
    }
    s_io_started = false;
    return true;
}

void lab02_page_provider_http_start_io(void)
{
    if (s_io_started || strlen(CONFIG_LAB02_SERVER_URL) == 0) {
        return;
    }
    s_io_started = true;
    s_boot_session_id = lab02_session_generation();
    if (!s_boot_worker) {
        if (xTaskCreate(boot_task, "lab02_http_boot", 8192, NULL, 4, &s_boot_worker) != pdPASS) {
            s_io_started = false;
            lab02_session_http_mark_stage(LAB02_LOAD_ERROR, s_boot_session_id);
        }
    }
}

bool lab02_page_provider_http_active(void)
{
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    return snap.http_active;
}
