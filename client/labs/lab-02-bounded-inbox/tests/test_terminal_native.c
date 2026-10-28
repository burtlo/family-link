#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include <assert.h>
#include <pthread.h>
#include <stdio.h>
#include <string.h>
#include <time.h>
extern void lab02_host_hold_admission(bool hold);
extern void lab02_host_hold_after_terminal(bool hold);
extern unsigned lab02_host_fetch_terminal_count(uint32_t op_id);
extern unsigned lab02_host_fetch_queue_depth(void);

static lab02_page_provider_t provider;
static lab02_inbox_page_t page;
static void pump(int count)
{
    for (int i = 0; i < count; i++)
        lab02_session_pump();
}
static void transport_burst(void)
{
    for (int i = 0; i < 10000; i++)
        lab02_session_intent_transport((i & 1) == 0);
}
static void *transport_thread(void *arg)
{
    (void)arg;
    transport_burst();
    return NULL;
}
static void *late_failure(void *arg)
{
    lab02_fetch_req_t req = *(lab02_fetch_req_t *)arg;
    lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, req.session_id, req.op_id, req.page_index, NULL, false);
    return NULL;
}
static void deliver(lab02_fetch_req_t req, bool initial, int revision)
{
    memset(&page, 0, sizeof(page));
    page.revision = revision;
    page.total_count = 256;
    page.page_offset = req.page_offset;
    page.item_count = 1;
    snprintf(page.items[0].message_id, sizeof(page.items[0].message_id), "m%d", req.page_offset);
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, req.session_id, req.op_id,
                                      req.page_index, &page, initial);
}
static lab02_fetch_req_t start(bool finish)
{
    assert(lab02_session_begin_http(&provider));
    lab02_session_http_worker_started(true, lab02_session_generation());
    lab02_session_pump();
    lab02_fetch_req_t req;
    assert(lab02_session_http_initial_request(&req));
    assert(req.page_size == 16 && req.page_offset == 0 && req.attempt == 1);
    assert(req.response_read_timeout_us == 12000000 && req.revision == 0);
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, req.session_id);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, req.session_id);
    if (finish) {
        deliver(req, true, 1);
        pump(1);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
    }
    return req;
}
static lab02_fetch_req_t take(int expected)
{
    lab02_fetch_req_t req;
    assert(lab02_session_http_take_fetch_request(&req));
    assert(req.page_index == expected);
    assert(req.page_offset == expected * req.page_size);
    return req;
}
int main(int argc, char **argv)
{
    assert(argc == 2);
    printf("TERMINAL_SCENARIO %s\n", argv[1]);
    if (!strcmp(argv[1], "initial-saturation")) {
        lab02_fetch_req_t boot = start(false), req[8];
        for (int i = 0; i < 8; i++) {
            assert(lab02_session_http_enqueue_fetch(i + 1, NULL));
            req[i] = take(i + 1);
        }
        transport_burst();
        deliver(boot, true, 1);
        deliver(boot, true, 1);
        for (int i = 0; i < 8; i++)
            lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, req[i].session_id, req[i].op_id, req[i].page_index, NULL, false);
        assert(!lab02_session_http_enqueue_fetch(9, NULL));
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_SHELL);
        pump(20);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        assert(lab02_session_metrics().fetch_fail == 0);
        assert(lab02_session_metrics().apply_stale == 8); /* revision-0 admissions superseded by boot revision */
        assert(lab02_session_metrics().apply_ok == 1);
        assert(lab02_session_metrics().fetch_event_dropped == 0);
        assert(lab02_session_snapshot().transport_unreachable);
    } else if (!strcmp(argv[1], "focus-full") || !strcmp(argv[1], "focus-busy")) {
        start(true);
        lab02_fetch_req_t admitted[8];
        admitted[0] = take(1);
        bool full = !strcmp(argv[1], "focus-full");
        if (full) {
            for (int i = 1; i < 8; i++) {
                assert(lab02_session_http_enqueue_fetch(i + 1, NULL));
                admitted[i] = take(i + 1);
            }
        } else
            lab02_host_hold_admission(true);
        lab02_session_intent_scroll_lock(true);
        lab02_session_intent_focus(144);
        lab02_session_intent_focus(176); /* Only page 11 may be retained. */
        assert(lab02_session_snapshot().focus_index == 176);
        if (full) {
            deliver(admitted[0], false, 1);
            pump(1);
        } else
            lab02_host_hold_admission(false);
        struct timespec delay = {.tv_nsec = 25000000};
        nanosleep(&delay, NULL);
        pump(1);
        lab02_fetch_req_t latest = take(11);
        assert(latest.page_index == 11 && latest.revision == 1);
        pump(10);
        assert(lab02_host_fetch_queue_depth() == 0);
        assert(lab02_session_snapshot().focus_index == 176);
        assert(lab02_session_snapshot().scroll_locked);
    } else if (!strcmp(argv[1], "initial-timeout")) {
        lab02_fetch_req_t boot = start(false);
        transport_burst();
        lab02_session_http_fetch_finished(LAB02_FETCH_TIMEOUT, boot.session_id, boot.op_id, 0, NULL, true);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, boot.session_id, boot.op_id, 0, NULL, true);
        pump(1);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
        assert(lab02_session_metrics().fetch_timeout == 1);
        assert(lab02_session_metrics().fetch_fail == 0);
        assert(lab02_session_metrics().stage_rejected == 0);
        lab02_session_http_mark_stage(LAB02_LOAD_READY, boot.session_id);
        pump(1);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_ERROR);
        assert(lab02_session_metrics().stage_rejected == 1);
    } else if (!strcmp(argv[1], "normal-saturation")) {
        start(true);
        lab02_fetch_req_t req[8];
        req[0] = take(1);
        for (int i = 1; i < 8; i++) {
            assert(lab02_session_http_enqueue_fetch(i + 1, NULL));
            req[i] = take(i + 1);
        }
        lab02_session_intent_scroll_lock(true);
        deliver(req[0], false, 1);
        for (int i = 1; i < 8; i++) {
            lab02_session_http_fetch_finished(LAB02_FETCH_TIMEOUT, req[i].session_id, req[i].op_id, req[i].page_index, NULL, false);
            lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, req[i].session_id, req[i].op_id, req[i].page_index, NULL, false);
        }
        pthread_t thread;
        assert(!pthread_create(&thread, NULL, transport_thread, NULL));
        pump(10000);
        pthread_join(thread, NULL);
        assert(lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_metrics().fetch_timeout == 7);
        assert(lab02_session_snapshot().scroll_locked);
        lab02_session_intent_scroll_lock(false);
        pump(20);
        assert(lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_metrics().fetch_timeout == 7);
        assert(lab02_session_metrics().fetch_event_dropped == 0);
        assert(lab02_session_http_enqueue_fetch(9, NULL));
    } else if (!strcmp(argv[1], "pending-mutation")) {
        start(true);
        lab02_fetch_req_t old = take(1);
        lab02_session_intent_focus(1); /* Arrival-time index must not replace final intent. */
        assert(lab02_session_intent_refresh());
        lab02_fetch_req_t refresh = take(0);
        lab02_session_intent_scroll_lock(true);
        deliver(refresh, false, 2);
        pump(1);
        assert(lab02_session_snapshot().pending_inbox_revision == 2);
        assert(lab02_session_snapshot().inbox_revision == 1);
        /* Old-model pages and failure/transport continue while mutation waits. */
        deliver(old, false, 1);
        pump(1);
        assert(lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_http_enqueue_fetch(2, NULL));
        lab02_fetch_req_t failure = take(2);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, failure.session_id, failure.op_id, 2, NULL, false);
        lab02_session_intent_transport(false);
        pump(1);
        assert(lab02_session_metrics().fetch_fail == 1);
        assert(lab02_session_snapshot().transport_unreachable);
        /* A newer response from another admitted page requests a page0 refresh. */
        assert(lab02_session_http_enqueue_fetch(3, NULL));
        lab02_fetch_req_t trigger = take(3);
        deliver(trigger, false, 3);
        pump(1);
        refresh = take(0);
        deliver(refresh, false, 3);
        pump(1);
        assert(lab02_session_snapshot().pending_inbox_revision == 3);
        assert(lab02_session_snapshot().inbox_revision == 1);
        /* Latest owner intent overrides the arrival-time viewed identity. */
        lab02_session_intent_focus(0);
        lab02_session_intent_scroll_lock(false);
        assert(lab02_session_snapshot().inbox_revision == 3);
        assert(!lab02_session_snapshot().pending_inbox_revision);
        assert(lab02_session_snapshot().focus_index == 0);
        assert(!strcmp(lab02_inbox_model_item(provider.model, 0)->message_id, "m0"));
        deliver(old, false, 1);
        pump(1); /* Already-terminal late delivery is rejected. */
    } else if (!strcmp(argv[1], "pending-session-reset")) {
        start(true);
        lab02_fetch_req_t old = take(1);
        assert(lab02_session_intent_refresh());
        lab02_fetch_req_t refresh = take(0);
        lab02_session_intent_scroll_lock(true);
        deliver(refresh, false, 2);
        pump(1);
        assert(lab02_session_snapshot().pending_inbox_revision == 2);
        lab02_fetch_req_t boot = start(false);
        assert(!lab02_session_snapshot().pending_inbox_revision);
        deliver(boot, true, 4);
        pump(4);
        deliver(old, false, 1);
        pump(1);
        assert(lab02_session_snapshot().inbox_revision == 4);
        assert(!lab02_session_snapshot().pending_inbox_revision);
    } else if (!strcmp(argv[1], "newer-operation")) {
        start(true);
        lab02_fetch_req_t old = take(1);
        assert(lab02_session_intent_refresh());
        lab02_fetch_req_t refresh = take(0);
        deliver(refresh, false, 2);
        pump(1);
        lab02_fetch_req_t current = take(1);
        assert(current.op_id != old.op_id && current.revision == 2);
        pump(1); /* Owner stale terminal before the old producer returns. */
        pthread_t old_thread, transport;
        assert(!pthread_create(&old_thread, NULL, late_failure, &old));
        assert(!pthread_create(&transport, NULL, transport_thread, NULL));
        assert(!lab02_session_http_enqueue_fetch(1, NULL));
        deliver(current, false, 2);
        deliver(current, false, 2);
        pump(10000);
        pthread_join(old_thread, NULL);
        pthread_join(transport, NULL);
        pump(3);
        assert(lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_snapshot().inbox_revision == 2);
        assert(lab02_session_metrics().fetch_fail == 0);
        assert(lab02_session_metrics().apply_stale == 1);
    } else if (!strcmp(argv[1], "response-old-revision")) {
        start(true);
        lab02_fetch_req_t old = take(1);
        deliver(old, false, 0);
        pump(1);
        assert(!lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_metrics().apply_stale == 1);
        assert(lab02_session_http_enqueue_fetch(1, NULL));
        lab02_fetch_req_t current = take(1);
        assert(current.op_id != old.op_id);
        deliver(current, false, 1);
        pump(1);
        deliver(current, false, 1);
        pump(1);
        assert(lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_metrics().apply_ok == 2);
    } else if (!strcmp(argv[1], "stale-session")) {
        start(true);
        lab02_fetch_req_t old = take(1);
        lab02_fetch_req_t boot = start(false);
        pump(1); /* terminal stale without waiting for old producer */
        deliver(old, false, 1);
        deliver(boot, true, 1);
        pump(4);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        assert(!lab02_inbox_model_item(provider.model, 16));
        assert(lab02_session_metrics().apply_stale == 0);
        assert(lab02_session_metrics().apply_ok == 1);
    } else if (!strcmp(argv[1], "pending-boot-reset")) {
        lab02_fetch_req_t old_boot = start(false);
        ((lab02_inbox_model_t *)provider.model)->count = 32; /* Seed only the admission-boundary fixture. */
        uint32_t old_page_op = 0;
        assert(lab02_session_http_enqueue_fetch(1, &old_page_op));
        assert(lab02_host_fetch_queue_depth() == 1);

        lab02_fetch_req_t new_boot = start(false);
        assert(new_boot.session_id != old_boot.session_id);
        assert(new_boot.op_id != old_boot.op_id);
        assert(lab02_host_fetch_queue_depth() == 0);

        /* Both old operations arrive after re-entry; neither may enter the new ledger. */
        lab02_session_http_fetch_finished(LAB02_FETCH_OK, old_boot.session_id, old_boot.op_id,
                                          old_boot.page_index, &page, true);
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, old_boot.session_id, old_page_op,
                                          1, NULL, false);
        pump(1);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_FETCHING);
        assert(lab02_session_materialized_count() == 0);

        deliver(new_boot, true, 4);
        pump(4);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        assert(lab02_session_snapshot().inbox_revision == 4);
        assert(lab02_session_metrics().apply_ok == 1);
    } else if (!strcmp(argv[1], "busy-reset")) {
        lab02_fetch_req_t old_boot = start(false);
        lab02_host_hold_admission(true);

        /* Reset and a late producer completion must remain bounded while the
         * presentation owner's delivery mutex is held elsewhere. */
        lab02_session_reset();
        lab02_session_http_fetch_finished(LAB02_FETCH_OK, old_boot.session_id,
                                          old_boot.op_id, old_boot.page_index,
                                          &page, true);
        pump(1);

        lab02_host_hold_admission(false);
        pump(2); /* Deferred cleanup releases the old boot reservation. */
        assert(lab02_host_fetch_queue_depth() == 0);

        lab02_fetch_req_t new_boot = start(false);
        assert(new_boot.session_id != old_boot.session_id);
        assert(new_boot.op_id != old_boot.op_id);
        lab02_session_http_fetch_finished(LAB02_FETCH_OK, old_boot.session_id,
                                          old_boot.op_id, old_boot.page_index,
                                          &page, true);
        pump(1);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_FETCHING);
        assert(lab02_session_materialized_count() == 0);

        deliver(new_boot, true, 5);
        pump(4);
        assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);
        assert(lab02_session_snapshot().inbox_revision == 5);
        assert(lab02_session_metrics().apply_ok == 1);
    } else if (!strcmp(argv[1], "terminal-contention-reset")) {
        start(true);
        lab02_fetch_req_t admitted = take(1);

        /* Force the owner to emit this operation's terminal while the
         * delivery mutex becomes unavailable for reconciliation. Reset then
         * gets the mutex before the deferred owner cleanup can run. */
        lab02_host_hold_after_terminal(true);
        deliver(admitted, false, 1);
        pump(1);
        lab02_host_hold_admission(false);
        lab02_session_reset();

        assert(lab02_host_fetch_terminal_count(admitted.op_id) == 1);
        pump(2);
        assert(lab02_host_fetch_queue_depth() == 0);
    } else
        assert(!"unknown scenario");
    puts("TERMINAL_RESULT PASS");
    return 0;
}
