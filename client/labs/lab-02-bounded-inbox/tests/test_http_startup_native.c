/* Production provider + reducer; deterministic RTOS task allocation and HTTP
 * adapters. Each process models a single application startup. */
#include "lab02/page_provider_http.h"
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02_platform/wifi.h"
#include "esp_http_client.h"
#include "freertos/task.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>

static void (*boot_entry)(void *);
static void *boot_arg;
static unsigned task_calls, deleted, http_calls;
static int fail_task;
static bool owner_pumping;
static size_t read_offset;
static const char body[] = "{\"schema\":\"fl.inbox-page/2\",\"inbox_revision\":1,\"total_count\":64,\"page_offset\":0,\"items\":[{\"message_id\":\"m0\",\"sender_name\":\"One\"},{\"message_id\":\"m1\",\"sender_name\":\"Two\"}]}";
extern unsigned lab02_host_fetch_queue_depth(void);

BaseType_t xTaskCreate(void (*task)(void *), const char *name, unsigned stack,
                      void *arg, unsigned priority, TaskHandle_t *handle)
{
    assert(!owner_pumping); /* No task allocation in the presentation pump. */
    assert(stack == 10240 && priority == 4);
    task_calls++;
    printf("TASK_CREATE name=%s ordinal=%u fail=%d\n", name, task_calls, (int)task_calls == fail_task);
    if ((int)task_calls == fail_task) return pdFALSE;
    if (handle) *handle = (void *)task;
    if (!strcmp(name, "lab02_http_boot")) { boot_entry = task; boot_arg = arg; }
    return pdPASS;
}
void vTaskDelete(TaskHandle_t task) { assert(!task); deleted++; }
void lab02_board_wifi_set_transport_notifier(lab02_wifi_transport_fn fn) { fn(true); }
bool lab02_board_wifi_connect(void) { return true; }
esp_http_client_handle_t esp_http_client_init(const esp_http_client_config_t *cfg)
{
    assert(strstr(cfg->url, "cursor=0&limit=16"));
    assert(cfg->timeout_ms == 3000);
    http_calls++; read_offset = 0; return (void *)body;
}
esp_err_t esp_http_client_open(esp_http_client_handle_t c, int length) { (void)c; assert(!length); return ESP_OK; }
int esp_http_client_get_errno(esp_http_client_handle_t c) { (void)c; return 0; }
void esp_http_client_cleanup(esp_http_client_handle_t c) { (void)c; }
int esp_http_client_fetch_headers(esp_http_client_handle_t c) { (void)c; return sizeof(body) - 1; }
int esp_http_client_get_status_code(esp_http_client_handle_t c) { (void)c; return 200; }
int esp_http_client_read(esp_http_client_handle_t c, char *buffer, int length)
{
    (void)c;
    size_t remaining = strlen(body) - read_offset;
    size_t n = remaining < (size_t)length ? remaining : (size_t)length;
    memcpy(buffer, body + read_offset, n); read_offset += n; return (int)n;
}
static void pump(unsigned count)
{
    owner_pumping = true;
    for (unsigned i = 0; i < count; i++) lab02_session_pump();
    owner_pumping = false;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    if (!strcmp(argv[1], "boot-fail")) fail_task = 1;
    else if (!strcmp(argv[1], "background-fail")) fail_task = 2;
    else assert(!strcmp(argv[1], "success"));
    lab02_page_provider_t pp;
    assert(lab02_page_provider_http_init(&pp));
    uint32_t session = lab02_session_generation();
    assert(!lab02_session_http_enqueue_fetch(1, NULL));
    lab02_page_provider_http_start_io();
    lab02_page_provider_http_start_io();
    assert(task_calls == 1);
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_SHELL);
    assert(!lab02_session_http_enqueue_fetch(1, NULL));
    if (fail_task != 1) { assert(boot_entry); boot_entry(boot_arg); }
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_SHELL);
    assert(!lab02_session_http_enqueue_fetch(1, NULL)); /* pending availability is owner-applied */
    assert(!lab02_host_fetch_queue_depth());
    if (fail_task == 2) lab02_session_intent_scroll_lock(true);
    pump(20);
    lab02_fetch_req_t initial;
    assert(!lab02_session_http_initial_request(&initial)); /* reservation reconciled */
    assert(lab02_session_metrics().stage_rejected == 0);
    assert(lab02_session_metrics().fetch_event_dropped == 0);
    if (fail_task) {
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
        assert(!lab02_host_fetch_queue_depth());
        assert(!lab02_session_http_enqueue_fetch(1, NULL));
        assert(!lab02_session_intent_refresh());
        lab02_session_http_worker_started(true, session); /* duplicate cannot reopen admission */
        lab02_session_http_worker_started(true, session - 1); /* stale cannot reopen it */
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session, 0, 0, NULL, true);
        pump(5);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
        assert(!lab02_session_http_enqueue_fetch(1, NULL));
        lab02_session_intent_focus(16); pump(5);
        assert(!lab02_host_fetch_queue_depth());
        lab02_page_provider_http_start_io();
        assert(task_calls == (unsigned)fail_task); /* no automatic startup retry */
        if (fail_task == 1) {
            assert(!http_calls && !deleted);
            assert(lab02_session_metrics().fetch_fail == 1);
            assert(!lab02_inbox_model_item(pp.model, 0));
        } else {
            assert(http_calls == 1 && deleted == 1);
            assert(lab02_session_metrics().apply_ok == 1);
            assert(lab02_session_metrics().fetch_fail == 0);
            assert(!strcmp(lab02_inbox_model_item(pp.model, 0)->message_id, "m0"));
            assert(!strcmp(lab02_inbox_model_item(pp.model, 1)->message_id, "m1"));
            assert(lab02_session_snapshot().total_count == 64);
        }
    } else {
        assert(task_calls == 2 && http_calls == 1 && deleted == 1);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        assert(lab02_host_fetch_queue_depth() == 1); /* prefetch only after usable consumer */
        lab02_fetch_req_t req;
        assert(lab02_session_http_take_fetch_request(&req) && req.page_index == 1);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, session, req.op_id, 1, NULL, false);
        pump(5);
        assert(lab02_inbox_model_item(pp.model, 0));
        assert(lab02_inbox_model_item(pp.model, 1));
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        lab02_page_provider_http_notify_initial_ready(1, 64); /* no duplicate ready claim */
    }
    printf("HTTP_STARTUP_RESULT scenario=%s PASS\n", argv[1]);
    return 0;
}
