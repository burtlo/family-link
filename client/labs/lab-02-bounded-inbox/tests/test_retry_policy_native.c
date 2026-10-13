/* Owner tap retry policy for failed pages and initial inbox ERROR (F006 / APP-O-04). */
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include <assert.h>
#include <stdio.h>
#include <string.h>
extern unsigned lab02_host_fetch_queue_depth(void);

static lab02_page_provider_t provider;

static lab02_inbox_page_t page_one(void)
{
    lab02_inbox_page_t page = {.revision = 1, .total_count = 64, .page_offset = 16, .item_count = 16};
    for (int i = 0; i < 16; i++) {
        snprintf(page.items[i].message_id, sizeof(page.items[i].message_id), "m%d", 16 + i);
        snprintf(page.items[i].sender_name, sizeof(page.items[i].sender_name), "S%d", 16 + i);
    }
    return page;
}

static void pump(unsigned n)
{
    for (unsigned i = 0; i < n; i++) {
        lab02_session_pump();
    }
}

static void seed_ready(void)
{
    assert(lab02_session_begin_http(&provider));
    uint32_t session = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session);
    lab02_session_http_worker_started(true, session);
    lab02_fetch_req_t boot;
    assert(lab02_session_http_initial_request(&boot));
    pump(2);
    lab02_inbox_page_t zero = {.revision = 1, .total_count = 64, .page_offset = 0, .item_count = 16};
    for (int i = 0; i < 16; i++) {
        snprintf(zero.items[i].message_id, sizeof(zero.items[i].message_id), "m%d", i);
    }
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, boot.session_id, boot.op_id, 0, &zero, true);
    pump(5);
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
}

int main(void)
{
    seed_ready();
    lab02_session_intent_focus(16);
    pump(2);
    lab02_fetch_req_t req;
    assert(lab02_session_http_take_fetch_request(&req));
    assert(req.page_index == 1);
    assert(req.attempt == 1);
    lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, req.session_id, req.op_id, 1, NULL, false);
    pump(3);
    assert(lab02_session_snapshot().focused_page_failed);
    uint32_t retries = lab02_session_metrics().fetch_retry;
    pump(120);
    assert(lab02_session_metrics().fetch_retry == retries);
    assert(lab02_session_intent_retry_page_for_index(16));
    lab02_fetch_req_t retry;
    bool got_retry = false;
    while (!got_retry) {
        assert(lab02_session_http_take_fetch_request(&req));
        if (req.page_index == 1 && req.attempt == 2) {
            retry = req;
            got_retry = true;
        }
    }
    assert(lab02_session_metrics().fetch_retry == retries + 1);
    unsigned depth_after_retry = lab02_host_fetch_queue_depth();
    assert(lab02_session_intent_retry_page_for_index(16));
    assert(lab02_session_intent_retry_page_for_index(16));
    assert(lab02_host_fetch_queue_depth() == depth_after_retry);
    lab02_inbox_page_t page = page_one();
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, retry.session_id, retry.op_id, 1, &page, false);
    pump(3);
    assert(lab02_inbox_model_item(provider.model, 16) != NULL);
    assert(!lab02_session_snapshot().focused_page_failed);

    lab02_session_reset();
    assert(lab02_session_begin_http(&provider));
    lab02_session_http_worker_started(false, lab02_session_generation());
    lab02_fetch_req_t boot2;
    assert(lab02_session_http_initial_request(&boot2));
    lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, boot2.session_id, boot2.op_id, 0, NULL, true);
    pump(5);
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
    assert(lab02_session_intent_retry_initial_load());
    pump(2);
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_SHELL);
    uint32_t session2 = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session2);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session2);
    lab02_session_http_worker_started(true, session2);
    pump(2);
    assert(lab02_session_http_initial_request(&boot2));
    assert(boot2.attempt == 2);
    lab02_inbox_page_t zero2 = {.revision = 1, .total_count = 64, .page_offset = 0, .item_count = 16};
    for (int i = 0; i < 16; i++) {
        snprintf(zero2.items[i].message_id, sizeof(zero2.items[i].message_id), "m%d", i);
    }
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, boot2.session_id, boot2.op_id, 0, &zero2, true);
    pump(5);
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
    printf("RETRY_POLICY_RESULT PASS\n");
    return 0;
}
