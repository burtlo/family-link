#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define PUMP_WAIT_MAX 300

static atomic_int done;
static lab02_fetch_req_t req1;
static lab02_fetch_req_t req2;

/* MinGW nanosleep(1ms) is much coarser than 1ms; keep host L1 timing predictable. */
static void pump_wait(void)
{
#if defined(_WIN32)
    Sleep(1);
#else
    struct timespec delay = {.tv_sec = 0, .tv_nsec = 1000000};
    nanosleep(&delay, NULL);
#endif
}

static void pump_until_ready(void)
{
    for (int i = 0; i < PUMP_WAIT_MAX && lab02_session_snapshot().load_stage != LAB02_LOAD_READY;
         i++) {
        lab02_session_pump();
        pump_wait();
    }
}

static void *send_initial(void *arg)
{
    (void)arg;
    static lab02_inbox_page_t page;
    page.revision = 1;
    page.total_count = 256;
    page.item_count = 1;
    page.page_offset = 0;
    strcpy(page.items[0].message_id, "m0");
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, lab02_session_generation(), 0, 0,
                                      &page, true);
    return NULL;
}

static void *send_two_pages(void *arg)
{
    (void)arg;
    static lab02_inbox_page_t page;
    page.revision = 1;
    page.total_count = 256;
    page.item_count = 1;
    page.page_offset = 16;
    strcpy(page.items[0].message_id, "m16");
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, req1.session_id, req1.op_id, 1,
                                      &page, false);
    memset(&page, 0, sizeof(page));
    page.revision = 1;
    page.total_count = 256;
    page.item_count = 1;
    page.page_offset = 32;
    strcpy(page.items[0].message_id, "m32");
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, req2.session_id, req2.op_id, 2,
                                      &page, false);
    atomic_store(&done, 1);
    return NULL;
}

static void *send_revision(void *arg)
{
    lab02_fetch_req_t *request = (lab02_fetch_req_t *)arg;
    static lab02_inbox_page_t page;
    page.revision = 2;
    page.total_count = 255;
    page.item_count = 1;
    page.page_offset = 0;
    strcpy(page.items[0].message_id, "m16");
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, request->session_id,
                                      request->op_id, 0, &page, false);
    return NULL;
}

static void *send_failure(void *arg)
{
    lab02_fetch_req_t *request = (lab02_fetch_req_t *)arg;
    lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, request->session_id,
                                      request->op_id, request->page_index, NULL, false);
    return NULL;
}

static void *toggle_transport(void *arg)
{
    (void)arg;
    for (int i = 0; i < 500; i++) {
        lab02_session_intent_transport((i & 1) != 0);
    }
    return NULL;
}

int main(void)
{
    lab02_page_provider_t provider;
    assert(lab02_session_begin_http(&provider));
    uint32_t session_id = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session_id);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session_id);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, send_initial, NULL) == 0);
    pthread_join(thread, NULL);
    pump_until_ready();
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);

    assert(!lab02_session_snapshot().transport_unreachable);
    lab02_session_intent_transport(false);
    assert(!lab02_session_snapshot().transport_unreachable);
    lab02_session_pump();
    assert(lab02_session_snapshot().transport_unreachable);
    assert(lab02_session_take_presentation_dirty());
    lab02_session_intent_transport(true);
    for (int i = 0; i < 50; i++) {
        lab02_session_pump();
        pump_wait();
    }
    assert(!lab02_session_snapshot().transport_unreachable);

    assert(pthread_create(&thread, NULL, toggle_transport, NULL) == 0);
    for (int i = 0; i < 600; i++) {
        lab02_session_snapshot_t before = lab02_session_snapshot();
        lab02_session_pump();
        lab02_session_snapshot_t after = lab02_session_snapshot();
        assert(before.transport_unreachable == after.transport_unreachable);
        pump_wait();
    }
    pthread_join(thread, NULL);
    for (int i = 0; i < 100; i++) {
        lab02_session_pump();
    }
    assert(!lab02_session_snapshot().transport_unreachable);

    assert(lab02_session_http_take_fetch_request(&req1));
    assert(req1.page_index == 1);
    assert(lab02_session_http_enqueue_fetch(2, &req2.op_id));
    req2.page_index = 2;
    req2.session_id = lab02_session_generation();

    lab02_session_intent_scroll_lock(true);
    assert(pthread_create(&thread, NULL, send_two_pages, NULL) == 0);
    for (int i = 0; i < 200; i++) {
        lab02_session_pump();
        pump_wait();
    }
    assert(lab02_inbox_model_item(provider.model, 16) == NULL);
    pthread_join(thread, NULL);
    lab02_session_intent_scroll_lock(false);
    for (int i = 0; i < PUMP_WAIT_MAX; i++) {
        lab02_session_pump();
        pump_wait();
        if (lab02_inbox_model_item(provider.model, 32) != NULL) {
            break;
        }
    }
    assert(atomic_load(&done) == 1);
    assert(strcmp(lab02_inbox_model_item(provider.model, 16)->message_id, "m16") == 0);
    assert(strcmp(lab02_inbox_model_item(provider.model, 32)->message_id, "m32") == 0);
    assert(lab02_session_metrics().apply_deferred == 1);

    lab02_session_intent_focus(16);
    assert(lab02_session_intent_refresh());
    lab02_fetch_req_t refresh;
    assert(lab02_session_http_take_fetch_request(&refresh));
    if (refresh.page_index != 0) {
        assert(lab02_session_http_take_fetch_request(&refresh));
    }
    assert(refresh.page_index == 0);
    assert(pthread_create(&thread, NULL, send_revision, &refresh) == 0);
    pthread_join(thread, NULL);
    for (int i = 0; i < PUMP_WAIT_MAX && lab02_session_snapshot().inbox_revision != 2; i++) {
        lab02_session_pump();
        pump_wait();
    }
    assert(lab02_session_snapshot().inbox_revision == 2);
    assert(lab02_session_snapshot().focus_index == 0);
    assert(strcmp(lab02_inbox_model_item(provider.model, 0)->message_id, "m16") == 0);
    assert(lab02_inbox_model_item(provider.model, 16) == NULL);

    lab02_fetch_req_t failed;
    assert(lab02_session_http_take_fetch_request(&failed));
    assert(failed.page_index == 1);
    assert(pthread_create(&thread, NULL, send_failure, &failed) == 0);
    pthread_join(thread, NULL);
    for (int i = 0; i < PUMP_WAIT_MAX && lab02_session_metrics().fetch_fail == 0; i++) {
        lab02_session_pump();
        pump_wait();
    }
    assert(lab02_session_metrics().fetch_fail == 1);
    uint32_t retry_op = 0;
    assert(lab02_session_http_enqueue_fetch(1, &retry_op));
    assert(retry_op != failed.op_id);
    for (int p = 3; p <= 9; p++) {
        assert(lab02_session_http_enqueue_fetch(p, NULL));
    }
    assert(!lab02_session_http_enqueue_fetch(10, NULL));
    assert(lab02_session_metrics().fetch_queue_overflow == 1);
    return 0;
}
