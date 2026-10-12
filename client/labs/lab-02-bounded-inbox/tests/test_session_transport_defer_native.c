#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"

#include <assert.h>
#include <stdio.h>
#include <string.h>
#include <time.h>

#if defined(_WIN32)
#include <windows.h>
#endif

#define PUMP_WAIT_MAX 300

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

static void post_page(uint32_t session_id, uint32_t op_id, int page_index, const char *msg_id,
                      int page_offset)
{
    static lab02_inbox_page_t page;
    memset(&page, 0, sizeof(page));
    page.revision = 1;
    page.total_count = 256;
    page.item_count = 1;
    page.page_offset = page_offset;
    strcpy(page.items[0].message_id, msg_id);
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, session_id, op_id, page_index, &page,
                                      false);
}

int main(void)
{
    lab02_page_provider_t provider;
    assert(lab02_session_begin_http(&provider));
    uint32_t session_id = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, session_id);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, session_id);

    static lab02_inbox_page_t boot;
    boot.revision = 1;
    boot.total_count = 256;
    boot.item_count = 1;
    boot.page_offset = 0;
    strcpy(boot.items[0].message_id, "m0");
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, session_id, 0, 0, &boot, true);
    pump_until_ready();
    assert(lab02_session_snapshot().load_stage == LAB02_LOAD_READY);

    lab02_fetch_req_t req = {0};
    int page = 0;
    for (int candidate = 1; candidate < LAB02_HTTP_MAX_PAGES; candidate++) {
        if (lab02_session_http_enqueue_fetch(candidate, &req.op_id)) {
            page = candidate;
            break;
        }
        lab02_session_pump();
    }
    assert(page > 0);
    req.page_index = page;
    req.session_id = lab02_session_generation();

    lab02_session_intent_scroll_lock(true);
    int item_index = page * LAB02_DEFAULT_PAGE_SIZE;
    char msg_id[32];
    snprintf(msg_id, sizeof(msg_id), "m%d", item_index);
    post_page(req.session_id, req.op_id, page, msg_id, item_index);
    lab02_session_intent_transport(false);

    lab02_session_pump();

    assert(lab02_session_snapshot().scroll_locked);
    assert(lab02_session_snapshot().transport_unreachable);
    assert(lab02_inbox_model_item(provider.model, item_index) == NULL);
    assert(lab02_session_metrics().apply_deferred >= 1);

    lab02_session_intent_scroll_lock(false);
    assert(lab02_inbox_model_item(provider.model, item_index) != NULL);
    assert(strcmp(lab02_inbox_model_item(provider.model, item_index)->message_id, msg_id) == 0);
    return 0;
}
