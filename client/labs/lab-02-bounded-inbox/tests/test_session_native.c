#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"

#include <assert.h>
#include <pthread.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

static atomic_int first_ack;
static atomic_int done;
static lab02_fetch_req_t req1;
static lab02_fetch_req_t req2;

static void pause_ms(long ms)
{
    struct timespec delay = {.tv_sec = 0, .tv_nsec = ms * 1000000};
    nanosleep(&delay, NULL);
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
    atomic_store(&first_ack, 1);
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

int main(void)
{
    lab02_page_provider_t provider;
    assert(lab02_session_begin_http(&provider));
    uint32_t session_id = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session_id);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session_id);
    pthread_t thread;
    assert(pthread_create(&thread, NULL, send_initial, NULL) == 0);
    for (int i = 0; i < 1000 && lab02_session_snapshot().load_stage != LAB02_LOAD_READY; i++) {
        lab02_session_pump();
        pause_ms(1);
    }
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
    pthread_join(thread, NULL);
    assert(lab02_session_http_take_fetch_request(&req1));
    assert(req1.page_index == 1);
    assert(lab02_session_http_enqueue_fetch(2, &req2.op_id));
    req2.page_index = 2;
    req2.session_id = lab02_session_generation();

    lab02_session_intent_scroll_lock(true);
    assert(pthread_create(&thread, NULL, send_two_pages, NULL) == 0);
    for (int i = 0; i < 10; i++) {
        lab02_session_pump();
        pause_ms(1);
    }
    assert(atomic_load(&first_ack) == 0);
    assert(lab02_inbox_model_item(provider.model, 16) == NULL);
    lab02_session_intent_scroll_lock(false);
    for (int i = 0; i < 1000 && !atomic_load(&done); i++) {
        lab02_session_pump();
        pause_ms(1);
    }
    pthread_join(thread, NULL);
    assert(atomic_load(&first_ack) == 1 && atomic_load(&done) == 1);
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
    for (int i = 0; i < 1000 && lab02_session_snapshot().inbox_revision != 2; i++) {
        lab02_session_pump();
        pause_ms(1);
    }
    pthread_join(thread, NULL);
    assert(lab02_session_snapshot().inbox_revision == 2);
    assert(lab02_session_snapshot().focus_index == 0);
    assert(strcmp(lab02_inbox_model_item(provider.model, 0)->message_id, "m16") == 0);
    assert(lab02_inbox_model_item(provider.model, 16) == NULL);

    lab02_fetch_req_t failed;
    assert(lab02_session_http_take_fetch_request(&failed));
    assert(failed.page_index == 1);
    assert(pthread_create(&thread, NULL, send_failure, &failed) == 0);
    for (int i = 0; i < 1000 && lab02_session_metrics().fetch_fail == 0; i++) {
        lab02_session_pump();
        pause_ms(1);
    }
    pthread_join(thread, NULL);
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
