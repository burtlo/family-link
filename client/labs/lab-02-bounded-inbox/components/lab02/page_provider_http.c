#include "lab02/page_provider_http.h"

#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02/snap_id.h"
#include "lab02/trace_emit.h"
#include "lab02_platform/wifi.h"

#include "esp_http_client.h"
#include "esp_log.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "sdkconfig.h"

#include <stdatomic.h>
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
static bool s_http_ready_logged;
static char s_fetch_scratch[HTTP_BUF_SIZE];
static lab02_inbox_page_t s_result_page;
static TaskHandle_t s_worker;
static atomic_bool s_boot_running;
static lab02_fetch_req_t s_boot_request;
static bool ensure_workers(void);

static void on_wifi_transport(bool reachable)
{
    lab02_session_intent_transport(reachable);
}

static bool http_errno_timed_out(int eno)
{
    return eno == ETIMEDOUT || eno == EWOULDBLOCK;
}

static lab02_http_fetch_result_t http_fetch_page(const lab02_fetch_req_t *req, char *buf, size_t buf_len,
                                               size_t *out_len)
{
    if (strlen(CONFIG_LAB02_SERVER_URL) == 0) {
        return LAB02_HTTP_FETCH_FAIL;
    }
    char url[192];
    int url_len = snprintf(url, sizeof(url), "%s/v2/inbox?cursor=%d&limit=%d",
                           CONFIG_LAB02_SERVER_URL, req->page_offset,
                           (int)req->page_size);
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
    int64_t deadline_us = esp_timer_get_time() + req->response_read_timeout_us;
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
        lab02_session_http_fetch_started(req.session_id, req.op_id, req.page_index,
                                         esp_timer_get_time());
        int offset = req.page_offset;
        size_t len = 0;
        lab02_http_fetch_result_t res =
            http_fetch_page(&req, s_fetch_scratch, sizeof(s_fetch_scratch), &len);
        if (res == LAB02_HTTP_FETCH_OK &&
            !lab02_inbox_model_parse_v2_page(s_fetch_scratch, len, offset, &s_result_page)) {
            res = LAB02_HTTP_FETCH_FAIL;
        }
        if (res == LAB02_HTTP_FETCH_OK) {
            s_result_page.snap_id = lab02_snap_current_id();
            const lab02_trace_payload_t payload = {
                .fields = LAB02_TRACE_FIELD_OP_ID | LAB02_TRACE_FIELD_SNAP_ID |
                          LAB02_TRACE_FIELD_REVISION | LAB02_TRACE_FIELD_PAGE_RECEIPT_ID |
                          LAB02_TRACE_FIELD_RUN_ID | LAB02_TRACE_FIELD_SERVER_COMPLETED_NS,
                .op_id = req.op_id,
                .snap_id = s_result_page.snap_id,
                .revision = s_result_page.revision,
                .page_receipt_id = s_result_page.page_receipt_id[0] ?
                    s_result_page.page_receipt_id : "-",
                .run_id = s_result_page.run_id[0] ? s_result_page.run_id : "-",
                .server_completed_ns = (int64_t)s_result_page.server_completed_ns,
            };
            lab02_trace_emit_payload("page_fetch", req.op_id, offset, NULL, &payload);
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
    const lab02_fetch_req_t req = s_boot_request;
    uint32_t session_id = req.session_id;
    lab02_session_http_fetch_started(req.session_id, req.op_id, req.page_index,
                                     esp_timer_get_time());
    lab02_board_wifi_set_transport_notifier(on_wifi_transport);
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session_id);
    if (!lab02_board_wifi_connect()) {
        ESP_LOGE(TAG, "Wi-Fi failed");
        lab02_session_http_worker_started(false, session_id);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session_id, req.op_id, req.page_index, NULL,
                                          true);
        atomic_store(&s_boot_running, false);
        vTaskDelete(NULL);
        return;
    }
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session_id);
    size_t len = 0;
    lab02_http_fetch_result_t boot_res =
        http_fetch_page(&req, s_fetch_scratch, sizeof(s_fetch_scratch), &len);
    if (boot_res == LAB02_HTTP_FETCH_TIMEOUT) {
        ESP_LOGE(TAG, "initial page fetch timed out");
        lab02_trace_emit("fetch_timeout", 0, 0, NULL, "boot");
        lab02_session_http_worker_started(false, session_id);
        lab02_session_http_fetch_finished(LAB02_FETCH_TIMEOUT, session_id, req.op_id, req.page_index,
                                          NULL, true);
        atomic_store(&s_boot_running, false);
        vTaskDelete(NULL);
        return;
    }
    if (boot_res != LAB02_HTTP_FETCH_OK) {
        ESP_LOGE(TAG, "initial page fetch failed");
        lab02_session_http_worker_started(false, session_id);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session_id, req.op_id, req.page_index, NULL,
                                          true);
        atomic_store(&s_boot_running, false);
        vTaskDelete(NULL);
        return;
    }
    if (!lab02_inbox_model_parse_v2_page(s_fetch_scratch, len, 0, &s_result_page)) {
        lab02_session_http_worker_started(false, session_id);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session_id, req.op_id, req.page_index, NULL,
                                          true);
        atomic_store(&s_boot_running, false);
        vTaskDelete(NULL);
        return;
    }
    /* Task allocation stays on the I/O owner, outside the LVGL pump. Preserve
     * the valid boot page even if paging capacity cannot be created. */
    bool available = ensure_workers();
    lab02_session_http_worker_started(available, session_id);
    if (!available) {
        ESP_LOGW(TAG, "background fetch worker unavailable; page 0 retained");
        lab02_trace_emit("worker_spawn_fail", 0, 0, NULL, "background");
    }
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, session_id, req.op_id, req.page_index,
                                      &s_result_page, true);
    atomic_store(&s_boot_running, false);
    vTaskDelete(NULL);
}

void lab02_page_provider_http_notify_initial_ready(uint32_t inbox_revision, uint16_t total_count)
{
    if (s_http_ready_logged) {
        return;
    }
    s_http_ready_logged = true;
    printf("LAB02_HTTP_READY revision=%u items=%u\n", (unsigned)inbox_revision,
           (unsigned)total_count);
}

static bool ensure_workers(void)
{
    if (!s_worker) {
        if (xTaskCreate(worker_task, "lab02_http", 10240, NULL, 4, &s_worker) != pdPASS) {
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

static void start_boot(const lab02_fetch_req_t *request)
{
    s_boot_request = *request;
    s_io_started = true;
    s_http_ready_logged = false;
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_OP_KIND,
        .session_id = request->session_id,
        .op_kind = "http_startup",
    };
    lab02_trace_emit_payload("io_start_admitted", 0, 0, NULL, &payload);
    atomic_store(&s_boot_running, true);
    if (xTaskCreate(boot_task, "lab02_http_boot", 10240, NULL, 4, NULL) != pdPASS) {
        lab02_session_http_worker_started(false, request->session_id);
        lab02_trace_emit("worker_spawn_fail", 0, 0, NULL, "boot");
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, request->session_id,
                                          request->op_id, request->page_index, NULL, true);
        atomic_store(&s_boot_running, false);
    }
}

void lab02_page_provider_http_start_io(void)
{
    if (s_io_started || atomic_load(&s_boot_running) || strlen(CONFIG_LAB02_SERVER_URL) == 0) return;
    lab02_fetch_req_t request;
    if (lab02_session_http_initial_request(&request)) start_boot(&request);
}

bool lab02_page_provider_http_active(void)
{
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    return snap.http_active;
}

void lab02_page_provider_http_service_initial_retry(void)
{
    /* An old boot may have posted its terminal but not exited yet. Do not
     * overwrite its frozen input/scratch or consume the deferred restart. */
    if (atomic_load(&s_boot_running) || strlen(CONFIG_LAB02_SERVER_URL) == 0) return;
    lab02_fetch_req_t request;
    if (lab02_session_http_take_initial_restart(&request)) start_boot(&request);
}
