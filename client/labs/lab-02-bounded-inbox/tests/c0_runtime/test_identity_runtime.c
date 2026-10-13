/* Production owner/controller + actual LVGL, explicit transport adapters. */
#define main prior_regression_main
#include "test_carousel_baseline.c"
#undef main
static int removed = -1, removed_second = -1;
static lab02_inbox_page_t identity_page(int rev, int offset, int count, int shift)
{
    lab02_inbox_page_t p = make_page(rev, offset, count, shift);
    for (int i = 0; i < p.item_count; i++) {
        int id = offset + i + shift;
        if (removed >= 0 && id >= removed) id++;
        if (removed_second >= 0 && id >= removed_second) id++;
        snprintf(p.items[i].message_id, sizeof(p.items[i].message_id), "m%d", id);
        snprintf(p.items[i].sender_name, sizeof(p.items[i].sender_name), "Sender %d", id);
    }
    return p;
}
static void settle(int rev, int count, int shift)
{
    for (int n = 0; n < 40; n++) {
        while (lab02_host_fetch_queue_depth()) {
            lab02_fetch_req_t req;
            assert(lab02_session_http_take_fetch_request(&req));
            submit(req, identity_page(rev, req.page_offset, count, shift), false);
        }
        struct timespec delay = {.tv_nsec = 21000000}; nanosleep(&delay, NULL);
        advance(30);
        assert(lab02_session_materialized_count() <= count && count <= LAB02_INBOX_MAX_ITEMS);
        if (!lab02_session_snapshot().identity_resolving && !lab02_host_fetch_queue_depth()) break;
    }
    advance(CONFIG_LAB02_SLOW_BIND_MS + 30);
}
static void mutation(int rev, int count, int shift)
{
    assert(lab02_session_intent_refresh());
    submit(take_page(0), identity_page(rev, 0, count, shift), false);
}
int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0); assert(argc == 3);
    scenario = argv[1]; artifact_dir = argv[2];
    lv_init(); lv_display_t *d = lv_display_create(320, 240);
    lv_display_set_color_format(d, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(d, drawbuffer, NULL, sizeof(drawbuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(d, flush); assert(lab02_carousel_init(&carousel));
    uint32_t sid = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, sid);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, sid);
    lab02_fetch_req_t boot; assert(lab02_session_http_initial_request(&boot));
    submit(boot, identity_page(1, 0, 256, 0), true); settle(1, 256, 0);
    int viewed = !strcmp(scenario, "removed-last") ? 255 : 200;
    lab02_carousel_set_focus(&carousel, viewed, false); settle(1, 256, 0);
    checkpoint("viewed");
    int count = 156, expected = 100, expected_id = 200;
    if (!strcmp(scenario, "removed-next") || !strcmp(scenario, "removed-unknown")) {
        removed = 200; count--;
        expected_id = 201;
        if (!strcmp(scenario, "removed-unknown")) {
            removed_second = 201; count--; expected = 153; expected_id = 255;
        }
    } else if (!strcmp(scenario, "removed-last")) {
        removed = 255; count--; expected = 154; expected_id = 254;
    }
    if (!strcmp(scenario, "unmaterialized-intent")) {
        lab02_session_intent_focus(80); /* no m80 loaded: index intent, not old m200 */
        expected = 80; expected_id = 180;
    }
    lab02_fetch_req_t late = {0};
    if (!strcmp(scenario, "late-result")) {
        uint32_t op; assert(lab02_session_http_enqueue_fetch(4, &op)); late = take_page(4);
    }
    mutation(101, count, 100);
    assert(lab02_session_snapshot().identity_resolving == (strcmp(scenario, "unmaterialized-intent") != 0));
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) {
        lab02_card_view_t *v = &carousel.pool.views[i];
        if (v->bound_index >= 0) assert(v->state == LAB02_CARD_LOADING);
    }
    assert(!strstr(lv_label_get_text(carousel.count_lab), "Sender"));
    checkpoint("unresolved");
    if (!strcmp(scenario, "late-result")) {
        lab02_inbox_page_t old = identity_page(1, 64, 256, 0);
        lab02_session_http_fetch_finished(LAB02_FETCH_OK, late.session_id, late.op_id, 4, &old, false);
        lab02_carousel_pump(&carousel);
        assert(lab02_session_snapshot().inbox_revision == 101);
    } else if (!strcmp(scenario, "queue-pressure")) {
        uint32_t op;
        for (int p = 2; p <= 8; p++) assert(lab02_session_http_enqueue_fetch(p, &op));
        assert(!lab02_session_http_enqueue_fetch(9, &op));
    } else if (!strcmp(scenario, "admission-busy")) {
        submit(take_page(1), identity_page(101, 16, count, 100), false);
        lab02_host_hold_admission(true);
        struct timespec delay = {.tv_nsec = 21000000}; nanosleep(&delay, NULL);
        advance(30); /* pump remains nonblocking and preserves latest search */
        lab02_host_hold_admission(false);
    } else if (!strcmp(scenario, "superseded")) {
        lab02_carousel_set_focus(&carousel, 4, false); expected = 4; expected_id = 104;
    } else if (!strcmp(scenario, "new-revision")) {
        mutation(102, 146, 110); count = 146; expected = 90;
        settle(102, count, 110);
    } else if (!strcmp(scenario, "failure-retry") || !strcmp(scenario, "incomplete-retry") || !strcmp(scenario, "timeout-retry")) {
        lab02_fetch_req_t req = take_page(1);
        if (!strcmp(scenario, "incomplete-retry")) {
            lab02_inbox_page_t partial = identity_page(101, 16, count, 100);
            partial.item_count = 1;
            submit(req, partial, false);
        } else {
            lab02_session_http_fetch_finished(!strcmp(scenario, "timeout-retry") ? LAB02_FETCH_TIMEOUT : LAB02_FETCH_FAIL,
                                             req.session_id, req.op_id, req.page_index, NULL, false);
        }
        lab02_carousel_pump(&carousel); advance(30);
        assert(lab02_session_snapshot().focused_page_failed);
        checkpoint("failed");
        assert(lab02_session_intent_retry_page_for_index(lab02_session_snapshot().focus_index));
    }
    if (strcmp(scenario, "new-revision")) settle(101, count, 100);
    lab02_session_snapshot_t snap = lab02_session_snapshot();
    assert(!snap.identity_resolving && snap.focus_index == expected);
    char id[24]; snprintf(id, sizeof(id), "m%d", expected_id);
    assert(!strcmp(lab02_page_provider_item(&carousel.provider, expected)->message_id, id));
    checkpoint("terminal");
    printf("F009_RUNTIME scenario=%s PASS focus=%d id=%s revision=%u roots=%d\n", scenario, expected, id, snap.inbox_revision, LAB02_CARD_POOL_SIZE);
}
