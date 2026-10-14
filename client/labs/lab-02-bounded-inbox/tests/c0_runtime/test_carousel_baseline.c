/* Production C + actual LVGL: F001/C2 acceptance; remaining defects keep their
 * baseline characterization until their separately authorized units. */
#define lab02_trace_emit c0_discard_trace
#define lab02_trace_emit_payload c0_host_trace_payload
#define lab02_snap_current_id c0_discard_snap_id
#include "../session_host_stubs.c"
#undef lab02_trace_emit
#undef lab02_trace_emit_payload
#undef lab02_snap_current_id
#include "lab02/carousel_controller.h"
#include "src/core/lv_obj_scroll_private.h"
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02/instrumentation.h"
#include "lab02/page_provider_http.h"
#include <assert.h>
#include <stdio.h>
#include <stdatomic.h>
#include "sdkconfig.h"

static lab02_carousel_t carousel;
static unsigned char framebuffer[320 * 240 * 3];
static unsigned char drawbuffer[320 * 240 * 3];
static const char *scenario;
static const char *artifact_dir;
static unsigned snap_starts, snap_ends;
static unsigned starts_by_id[128], ends_by_id[128];
static lv_indev_data_t pointer_data;
static void pointer_read(lv_indev_t *indev, lv_indev_data_t *data)
{
    (void)indev; *data = pointer_data;
}
static void pointer_step(lv_indev_t *indev, int x, lv_indev_state_t state)
{
    pointer_data.point.x = x; pointer_data.point.y = LAB02_CARD_Y + 60;
    pointer_data.state = state;
    lv_indev_read(indev);
}

void lab02_trace_emit_payload(const char *event, uint32_t generation, int focus,
                              const char *message_id,
                              const lab02_trace_payload_t *payload)
{
    (void)generation;
    (void)focus;
    (void)message_id;
    if (!strcmp(event, "snap_start")) snap_starts++;
    if (!strcmp(event, "snap_end")) snap_ends++;
    if (!strcmp(event, "snap_start") || !strcmp(event, "snap_end")) {
        assert(payload && (payload->fields & LAB02_TRACE_FIELD_SNAP_ID));
        unsigned id = payload->snap_id;
        assert(id < 128);
        if (!strcmp(event, "snap_start")) assert(++starts_by_id[id] == 1);
        else assert(++ends_by_id[id] == 1 && starts_by_id[id] == 1);
    }
    printf("C0_TRACE scenario=%s event=%s\n", scenario, event);
}


void lab02_trace_emit(const char *event, uint32_t generation, int focus,
                      const char *message_id, const char *extra)
{
    if (!strcmp(event, "snap_start")) snap_starts++;
    if (!strcmp(event, "snap_end")) snap_ends++;
    if (!strcmp(event, "snap_start") || !strcmp(event, "snap_end")) {
        const char *field = strstr(extra, "snap_id="); unsigned id = 0;
        assert(field && sscanf(field, "snap_id=%u", &id) == 1 && id < 128);
        if (!strcmp(event, "snap_start")) assert(++starts_by_id[id] == 1);
        else assert(++ends_by_id[id] == 1 && starts_by_id[id] == 1);
    }
    printf("C0_TRACE scenario=%s event=%s gen=%u focus=%d msg=%s %s\n",
           scenario, event, generation, focus, message_id ? message_id : "-", extra ? extra : "");
}

/* Only provider initialization/item access is adapted; all merges/focus/queues
 * are production session code. No HTTP transport or task scheduling is claimed. */
bool lab02_page_provider_init(lab02_page_provider_t *pp)
{
#ifdef LAB02_REAL_HTTP_PROVIDER
    return lab02_page_provider_http_init(pp);
#else
    bool begun = lab02_session_begin_http(pp);
    if (begun) {
        lab02_session_http_worker_started(true, lab02_session_generation());
        lab02_session_pump();
    }
    return begun;
#endif
}
const lab02_inbox_item_t *lab02_page_provider_item(const lab02_page_provider_t *pp, int index)
{ return lab02_inbox_model_item(pp->model, index); }

void lab02_page_provider_http_retry_initial_load(void) {}
extern unsigned lab02_host_fetch_queue_depth(void);

static void flush(lv_display_t *display, const lv_area_t *area, uint8_t *pixels)
{
    int width = area->x2 - area->x1 + 1;
    for (int y = area->y1; y <= area->y2; y++) {
        memcpy(framebuffer + (y * 320 + area->x1) * 3,
               pixels + (y - area->y1) * width * 3, (size_t)width * 3);
    }
    lv_display_flush_ready(display);
}

static void assert_window(void);

static void advance(unsigned ms)
{
    for (unsigned i = 0; i < ms; i += 10) {
        lv_tick_inc(10);
        lv_timer_handler();
        lab02_carousel_pump(&carousel);
        assert_window();
    }
}

static void assert_window(void);

static void checkpoint(const char *name)
{
    assert_window();
    lv_obj_update_layout(carousel.screen);
    lv_refr_now(NULL);
    lab02_session_snapshot_t s = lab02_session_snapshot();
    const lab02_inbox_item_t *focus = lab02_page_provider_item(&carousel.provider, s.focus_index);
    printf("C0_STATE scenario=%s point=%s session=%u revision=%u focus=%d focus_id=%s snap_id=%u snap_locked=%d session_locked=%d scroll_x=%d window=",
           scenario, name, lab02_session_generation(), s.inbox_revision, s.focus_index,
           focus ? focus->message_id : "-", lab02_snap_current_id(), carousel.snap.scroll_lock,
           s.scroll_locked, (int)lv_obj_get_scroll_x(carousel.scroll));
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) {
        lab02_card_view_t *v = &carousel.pool.views[i];
        lv_area_t bounds;
        lv_obj_get_coords(v->root, &bounds);
        printf("%s{slot:%d,index:%d,id:%s,gen:%u,hidden:%d,x1:%d,x2:%d,state:%d,revision:%u,session:%u,sender:%s}",
               i ? "," : "", i, v->bound_index, v->bound_message_id[0] ? v->bound_message_id : "-",
               v->bind_generation, lv_obj_has_flag(v->root, LV_OBJ_FLAG_HIDDEN),
               (int)bounds.x1, (int)bounds.x2, v->state, v->inbox_revision, v->session_generation, lv_label_get_text(v->sender_lab));
    }
    printf(" starts=%u ends=%u deferred=%u stale=%u applied=%u\n", snap_starts, snap_ends,
           lab02_session_metrics().apply_deferred, lab02_session_metrics().apply_stale,
           lab02_session_metrics().apply_ok);
    char path[1024];
    snprintf(path, sizeof(path), "%s/%s-%s.ppm", artifact_dir, scenario, name);
    FILE *file = fopen(path, "wb");
    assert(file);
    fputs("P6\n320 240\n255\n", file);
    /* LVGL RGB888 draw buffer is BGR in this host runtime. */
    for (size_t i = 0; i < sizeof(framebuffer); i += 3) {
        fputc(framebuffer[i + 2], file); fputc(framebuffer[i + 1], file); fputc(framebuffer[i], file);
    }
    fclose(file);
}

static lab02_inbox_page_t make_page(unsigned revision, int offset, int count, int shift)
{
    lab02_inbox_page_t p = {.revision = revision, .total_count = (uint16_t)count,
                            .page_offset = offset, .item_count = 16};
    if (count - offset < 16) p.item_count = (uint16_t)(count - offset);
    for (int i = 0; i < p.item_count; i++) {
        int id = offset + i + shift;
        p.items[i].unread = true;
        p.items[i].duration_ms = 12345;
        snprintf(p.items[i].message_id, sizeof(p.items[i].message_id), "m%d", id);
        snprintf(p.items[i].sender_name, sizeof(p.items[i].sender_name), "Sender %d", id);
        snprintf(p.items[i].preview, sizeof(p.items[i].preview), "Fixture %d", id);
    }
    return p;
}

static lab02_inbox_page_t response;
static lab02_fetch_req_t request;
static bool initial_response;
static atomic_bool delivered;
static void *deliver(void *unused)
{
    (void)unused;
    lab02_session_http_fetch_finished(LAB02_FETCH_OK, request.session_id,
        request.op_id, request.page_index, &response, initial_response);
    atomic_store(&delivered, true);
    return NULL;
}

static void submit(lab02_fetch_req_t req, lab02_inbox_page_t page, bool initial)
{
    printf("C0_DELIVERY scenario=%s session=%u op_id=%u page=%d response_revision=%u initial=%d snap_id=%u\n",
           scenario, req.session_id, req.op_id, req.page_index, page.revision, initial, lab02_snap_current_id());
    request = req; response = page; initial_response = initial;
    atomic_store(&delivered, false);
    pthread_t worker;
    assert(!pthread_create(&worker, NULL, deliver, NULL));
    for (int i = 0; i < 2000 && !atomic_load(&delivered); i++) {
        lab02_carousel_pump(&carousel);
        struct timespec delay = {.tv_nsec = 1000000};
        nanosleep(&delay, NULL);
    }
    assert(atomic_load(&delivered));
    pthread_join(worker, NULL);
    lab02_carousel_pump(&carousel);
}

static lab02_fetch_req_t take_page(int page)
{
    lab02_fetch_req_t req;
    while (lab02_session_http_take_fetch_request(&req)) {
        if (req.page_index == page) {
            printf("C0_REQUEST scenario=%s session=%u op_id=%u page=%d submitted_us=%lld\n",
                   scenario, req.session_id, req.op_id, req.page_index, (long long)req.submitted_us);
            return req;
        }
    }
    assert(!"expected fetch request absent");
    return req;
}

static lab02_card_view_t *view_at(int index)
{
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++)
        if (carousel.pool.views[i].bound_index == index) return &carousel.pool.views[i];
    assert(!"visible binding missing"); return NULL;
}

static void assert_window(void)
{
    int x = lv_obj_get_scroll_x(carousel.scroll);
    for (int i = 0; i < carousel.item_count; i++) {
        int left = LAB02_CARD_GAP + i * (LAB02_CARD_W + LAB02_CARD_GAP);
        if (left < x + LAB02_LCD_W && left + LAB02_CARD_W > x) {
            lab02_card_view_t *v = view_at(i);
            assert(!lv_obj_has_flag(v->root, LV_OBJ_FLAG_HIDDEN));
            assert(v->state != LAB02_CARD_UNBOUND);
            if (v->state == LAB02_CARD_BOUND) {
                const lab02_inbox_item_t *item = lab02_page_provider_item(&carousel.provider, i);
                assert(item && !strcmp(v->bound_message_id, item->message_id));
            }
        }
    }
    assert(lv_obj_get_child_count(carousel.scroll) == 5);
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++)
        assert(lv_obj_get_child_count(carousel.pool.views[i].root) == 4);
}

static void assert_archived_ignored(lv_anim_t *archived)
{
    int x = lv_obj_get_scroll_x(carousel.scroll);
    int focus = lab02_session_snapshot().focus_index;
    unsigned terminals = snap_ends;
    uint32_t generations[LAB02_CARD_POOL_SIZE];
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) generations[i] = carousel.pool.views[i].bind_generation;
    archived->custom_exec_cb(archived, 999);
    archived->completed_cb(archived);
    archived->deleted_cb(archived);
    assert(lv_obj_get_scroll_x(carousel.scroll) == x && snap_ends == terminals);
    assert(lab02_session_snapshot().focus_index == focus);
    for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) assert(carousel.pool.views[i].bind_generation == generations[i]);
}

static void drag_to(int index)
{
    lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_BEGIN, NULL);
    /* Same raw displacement used by LVGL indev scrolling, avoiding the API
     * scroll_to wrapper which synthesizes a complete begin/end pair. */
    lv_obj_scroll_by_raw(carousel.scroll, lv_obj_get_scroll_x(carousel.scroll) -
                         lab02_snap_target_x_for_index(index, carousel.item_count), 0);
    lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL, NULL);
}

int main(int argc, char **argv)
{
    setvbuf(stdout, NULL, _IONBF, 0);
    assert(argc == 3);
    scenario = argv[1]; artifact_dir = argv[2];
    lv_init();
    lv_display_t *display = lv_display_create(320, 240);
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB888);
    lv_display_set_buffers(display, drawbuffer, NULL, sizeof(drawbuffer), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    assert(lab02_carousel_init(&carousel));
    uint32_t sid = lab02_session_generation();
    lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, sid);
    lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, sid);
    lab02_fetch_req_t boot;
    assert(lab02_session_http_initial_request(&boot));
    submit(boot, make_page(1, 0, 64, 0), true);
    lab02_carousel_set_focus(&carousel, 4, false);
    advance(CONFIG_LAB02_SLOW_BIND_MS + 20);
    checkpoint("ready");

    if (!strcmp(scenario, "fast-boundary")) {
        drag_to(10); assert_window(); /* Entirely local materialized content. */
        drag_to(40); assert_window(); /* Delayed multi-page jump. */
        drag_to(2); assert_window(); /* Reversal while finger remains down. */
        drag_to(20);
        checkpoint("drag20");
        /* Intermediate focus may be coalesced; end must commit latest intent. */
        lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_END, NULL);
        assert(lab02_session_snapshot().focus_index == 20);
        advance(300); checkpoint("terminal");
        lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_BEGIN, NULL);
        lv_obj_scroll_by_raw(carousel.scroll, lv_obj_get_scroll_x(carousel.scroll) - 566, 0);
        lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL, NULL);
        assert_window();
        assert(view_at(3) && view_at(4) && view_at(5) && view_at(6));
        checkpoint("four-partial");
    } else if (!strcmp(scenario, "delayed-focus")) {
        lab02_carousel_set_focus(&carousel, 20, false);
        checkpoint("missing");
        assert(view_at(carousel.focus_index)->bound_index == 20);
        assert(!lv_obj_has_flag(view_at(carousel.focus_index)->root, LV_OBJ_FLAG_HIDDEN));
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_LOADING);
        assert(!strcmp(lv_label_get_text(view_at(carousel.focus_index)->sender_lab), "Loading message…"));
        assert(!strcmp(lv_label_get_text(view_at(carousel.focus_index)->preview_lab), ""));
        assert(!strcmp(lv_label_get_text(view_at(carousel.focus_index)->dur_lab), ""));
        assert(lv_obj_has_flag(view_at(carousel.focus_index)->unread_dot, LV_OBJ_FLAG_HIDDEN));
        submit(take_page(1), make_page(1, 16, 64, 0), false);
        checkpoint("terminal");
    } else if (!strcmp(scenario, "failed-page")) {
        lab02_carousel_set_focus(&carousel, 15, false);
        lab02_fetch_req_t req = take_page(1);
        lab02_session_http_fetch_finished(LAB02_FETCH_TIMEOUT, req.session_id,
            req.op_id, req.page_index, NULL, false);
        lab02_carousel_pump(&carousel);
        advance(CONFIG_LAB02_SLOW_BIND_MS + 20);
        checkpoint("neighbor-error");
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_BOUND);
        assert(view_at(16)->state == LAB02_CARD_ERROR);
        assert(!strcmp(lv_label_get_text(view_at(16)->sender_lab), "Message unavailable"));
        lab02_carousel_set_focus(&carousel, 16, false);
        req = take_page(1); /* Existing focus admission policy is unchanged. */
        lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, req.session_id,
            req.op_id, req.page_index, NULL, false);
        lab02_carousel_pump(&carousel);
        checkpoint("focused-error");
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_ERROR);
        assert(!lv_obj_has_flag(view_at(carousel.focus_index)->root, LV_OBJ_FLAG_HIDDEN));
        assert(!strcmp(lv_label_get_text(view_at(carousel.focus_index)->preview_lab), ""));
        uint32_t auto_retries = lab02_session_metrics().fetch_retry;
        advance(120);
        assert(lab02_session_metrics().fetch_retry == auto_retries);
        while (lab02_host_fetch_queue_depth() > 0) {
            lab02_fetch_req_t pending;
            assert(lab02_session_http_take_fetch_request(&pending));
            if (pending.page_index != 1) {
                lab02_session_http_fetch_finished(LAB02_FETCH_FAIL, pending.session_id,
                    pending.op_id, pending.page_index, NULL, false);
                lab02_carousel_pump(&carousel);
            }
        }
        assert(lab02_session_intent_retry_page_for_index(16));
        submit(take_page(1), make_page(1, 16, 64, 0), false);
        advance(CONFIG_LAB02_SLOW_BIND_MS + 20);
        checkpoint("owner-retry-success");
        assert(view_at(16)->state == LAB02_CARD_BOUND);
        lab02_carousel_set_focus(&carousel, 40, false);
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_LOADING);
        (void)take_page(2); /* Traversal still submits the newly focused page. */
    } else if (!strcmp(scenario, "pool-bounds")) {
        lv_obj_t *screen = carousel.screen, *scroll = carousel.scroll;
        unsigned nodes = lv_obj_get_child_count(scroll);
        for (int n = 8; n <= 256; n = n == 8 ? 16 : n * 4) {
            assert(lab02_session_intent_refresh());
            submit(take_page(0), make_page((unsigned)n, 0, n, 0), false);
            lab02_carousel_set_focus(&carousel, n - 1, false);
            assert(lv_obj_get_child_count(scroll) == nodes);
            assert(carousel.screen == screen && carousel.scroll == scroll);
            for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++)
                assert(lv_obj_get_child_count(carousel.pool.views[i].root) == 4);
            assert_window();
            const int stride = LAB02_CARD_W + LAB02_CARD_GAP;
            const int offsets[] = {0, stride / 4, stride / 2, stride - 1};
            for (int idx = 0; idx < n; idx++) for (unsigned j = 0; j < 4; j++) {
                int x = idx * stride + offsets[j];
                int max = LAB02_CARD_GAP + n * stride - LAB02_LCD_W;
                if (x > max) x = max;
                lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_BEGIN, NULL);
                lv_obj_scroll_by_raw(carousel.scroll, lv_obj_get_scroll_x(carousel.scroll) - x, 0);
                lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL, NULL);
                assert_window();
            }
            lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_END, NULL);
            assert(lab02_session_snapshot().focus_index == n - 2);
            advance(300);
            checkpoint("bounded");
        }
    } else if (!strcmp(scenario, "slow-bind")) {
#if CONFIG_LAB02_SLOW_BIND_MS > 0
        unsigned stale = lab02_instrumentation_get()->stale_bind_drops;
        lab02_carousel_set_focus(&carousel, 10, false);
        uint32_t old_generation = view_at(carousel.focus_index)->bind_generation;
        checkpoint("pending6");
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_LOADING);
        assert(!strcmp(lv_label_get_text(view_at(carousel.focus_index)->preview_lab), ""));
        advance(20);
        lab02_carousel_set_focus(&carousel, 11, false);
        assert(view_at(10)->bind_generation == old_generation);
        assert(lab02_instrumentation_get()->stale_bind_drops >= stale);
        checkpoint("superseded7");
        advance(CONFIG_LAB02_SLOW_BIND_MS + 20);
        checkpoint("bound7");
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_BOUND);
        assert(!strcmp(lv_label_get_text(view_at(carousel.focus_index)->sender_lab), "Sender 11"));
        lab02_carousel_set_focus(&carousel, 8, false);
        lab02_card_view_t *old_view = view_at(8);
        lab02_session_reset(); /* Timer cannot apply across a new session. */
        advance(CONFIG_LAB02_SLOW_BIND_MS + 20);
        assert(old_view->state == LAB02_CARD_STALE);
        assert(!strcmp(lv_label_get_text(old_view->preview_lab), ""));
#else
        assert(!"slow-bind scenario requires production slow-bind compile branch");
#endif
    } else if (!strcmp(scenario, "reversal")) {
        lab02_carousel_set_focus(&carousel, 8, true);
        lv_anim_t archived = *lv_anim_get(&carousel.snap, NULL);
        uint32_t old_id = carousel.snap.operation_id;
        advance(60); checkpoint("snap8");
        drag_to(2);
        lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_END, NULL);
        checkpoint("reverse2");
        assert(lab02_session_snapshot().focus_index == 2);
        assert_archived_ignored(&archived);
        assert(ends_by_id[old_id] == 1);
        advance(300); checkpoint("latest-target-terminal");
        assert(lab02_session_snapshot().focus_index == 2);
        lab02_carousel_set_focus(&carousel, 10, true);
        advance(40);
        lab02_carousel_set_focus(&carousel, 5, true);
        advance(40);
        lab02_carousel_set_focus(&carousel, 7, true);
        advance(300); checkpoint("replacement-terminal");
        assert(lab02_session_snapshot().focus_index == 7);
        old_id = carousel.snap.operation_id;
        lab02_carousel_set_focus(&carousel, 7, true);
        assert(carousel.snap.operation_id > old_id && snap_starts == snap_ends);
        lab02_carousel_set_focus(&carousel, 12, true);
        advance(30);
        lab02_carousel_set_focus(&carousel, 3, false);
        advance(300);
        assert(lab02_session_snapshot().focus_index == 3 && snap_starts == snap_ends);

        /* Actual LVGL pointer driver, hit testing and gesture recognizer. */
        lv_indev_t *pointer = lv_indev_create();
        lv_indev_set_type(pointer, LV_INDEV_TYPE_POINTER);
        lv_indev_set_read_cb(pointer, pointer_read);
        lab02_carousel_set_focus(&carousel, 8, true);
        advance(50);
        old_id = carousel.snap.operation_id;
        pointer_step(pointer, 100, LV_INDEV_STATE_PRESSED);
        pointer_step(pointer, 150, LV_INDEV_STATE_PRESSED);
        pointer_step(pointer, 200, LV_INDEV_STATE_PRESSED);
        assert(ends_by_id[old_id] == 1 && !carousel.snap.scroll_lock);
        pointer_step(pointer, 200, LV_INDEV_STATE_RELEASED);
        advance(600); checkpoint("pointer-reversal-terminal");
        assert(lab02_session_snapshot().focus_index != 8);
        assert(snap_starts == snap_ends);
        lv_indev_delete(pointer);

        lab02_carousel_set_focus(&carousel, 10, true);
        archived = *lv_anim_get(&carousel.snap, NULL);
        lv_anim_delete(&carousel.snap, NULL);
        assert(!lab02_session_snapshot().scroll_locked && snap_starts == snap_ends);
        archived.completed_cb(&archived);
        /* Delete while active, then archived tick/completion after invalidation. */
        lab02_carousel_set_focus(&carousel, 12, true);
        archived = *lv_anim_get(&carousel.snap, NULL);
        lv_obj_delete(carousel.screen);
        assert(!carousel.scroll && !lab02_carousel_instance());
        archived.custom_exec_cb(&archived, 999);
        archived.completed_cb(&archived);
        archived.deleted_cb(&archived);
        lab02_carousel_pump(&carousel);
        assert(snap_starts == snap_ends);
        assert(lab02_carousel_init(&carousel));
        sid = lab02_session_generation();
        lab02_session_http_mark_stage(LAB02_LOAD_CONNECTING, sid);
        lab02_session_http_mark_stage(LAB02_LOAD_FETCHING, sid);
        assert(lab02_session_http_initial_request(&boot));
        submit(boot, make_page(1, 0, 64, 0), true);
        lab02_carousel_set_focus(&carousel, 11, true);
        archived = *lv_anim_get(&carousel.snap, NULL);
        lab02_session_reset();
        advance(300);
        archived.custom_exec_cb(&archived, 999);
        archived.completed_cb(&archived);
        assert(!carousel.snap.scroll_lock && snap_starts == snap_ends);
    } else if (!strcmp(scenario, "held-page")) {
        lab02_carousel_set_focus(&carousel, 20, false);
        lab02_session_intent_scroll_lock(true);
        checkpoint("loading-held");
        int32_t held_x = lv_obj_get_scroll_x(carousel.scroll);
        lv_obj_t *root = view_at(20)->root;
        assert(view_at(20)->state == LAB02_CARD_LOADING);
        submit(take_page(1), make_page(1, 16, 64, 0), false);
        advance(1200);
        checkpoint("bound-held");
        assert(lab02_session_snapshot().scroll_locked);
        assert(lv_obj_get_scroll_x(carousel.scroll) == held_x);
        assert(view_at(20)->root == root);
        assert(view_at(carousel.focus_index)->state == LAB02_CARD_BOUND);
        assert(!strcmp(view_at(20)->bound_message_id, "m20"));
        lab02_session_intent_scroll_lock(false);
    } else if (!strcmp(scenario, "page-during-snap")) {
        lab02_carousel_set_focus(&carousel, 20, true);
        advance(60);
        int32_t arrival_x = lv_obj_get_scroll_x(carousel.scroll);
        lv_obj_t *roots[LAB02_CARD_POOL_SIZE];
        for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) {
            roots[i] = carousel.pool.views[i].root;
        }
        submit(take_page(1), make_page(1, 16, 64, 0), false);
        assert(lv_obj_get_scroll_x(carousel.scroll) == arrival_x);
        assert(carousel.snap.scroll_lock && lab02_session_snapshot().scroll_locked);
        for (int i = 0; i < LAB02_CARD_POOL_SIZE; i++) {
            assert(carousel.pool.views[i].root == roots[i]);
#if CONFIG_LAB02_SLOW_BIND_MS == 0
            if (carousel.pool.views[i].bound_index >= 0)
                assert(carousel.pool.views[i].state == LAB02_CARD_BOUND);
#else
            if (carousel.pool.views[i].bound_index >= 0)
                assert(carousel.pool.views[i].state == LAB02_CARD_LOADING ||
                       carousel.pool.views[i].state == LAB02_CARD_BOUND);
#endif
        }
        checkpoint("arrival");
        assert(lab02_page_provider_item(&carousel.provider, 20));
        advance(300); checkpoint("terminal");
        assert(lab02_page_provider_item(&carousel.provider, 20));
    } else if (!strcmp(scenario, "push-during-snap") || !strcmp(scenario, "remove-during-snap")) {
        bool removal = !strcmp(scenario, "remove-during-snap");
        /* Poll is submitted before input; its remote revision arrives during snap. */
        assert(lab02_session_intent_refresh());
        lab02_fetch_req_t mutation_req = take_page(0);
        lab02_carousel_set_focus(&carousel, 8, true);
        advance(60);
        assert(!lab02_session_intent_refresh());
        printf("C0_TRACE scenario=%s event=refresh_intent_rejected_during_snap\n", scenario);
        lab02_inbox_page_t mutation = make_page(2, 0, removal ? 63 : 65, 0);
        if (removal) {
            for (int i = 8; i < 16; i++) {
                snprintf(mutation.items[i].message_id, sizeof(mutation.items[i].message_id), "m%d", i + 1);
                snprintf(mutation.items[i].sender_name, sizeof(mutation.items[i].sender_name), "Sender %d", i + 1);
            }
        }
        /* Fixture server push appends; existing IDs retain their indices. */
        submit(mutation_req, mutation, false);
        checkpoint("arrival");
        assert(lab02_session_snapshot().inbox_revision == 1);
        assert(lab02_session_snapshot().pending_inbox_revision == 2);
        advance(300); checkpoint("terminal");
        assert(lab02_session_snapshot().inbox_revision == 2);
        const lab02_inbox_item_t *item = lab02_page_provider_item(&carousel.provider, lab02_session_snapshot().focus_index);
        assert(item && !strcmp(item->message_id, removal ? "m9" : "m8"));
        /* A second accepted mutation must reconcile replacement intent, not the
         * old snap target. Insertion relocates m6; removal chooses next m7. */
        assert(lab02_session_intent_refresh());
        mutation_req = take_page(0);
        lab02_carousel_set_focus(&carousel, 10, true);
        advance(40);
        lv_anim_t archived = *lv_anim_get(&carousel.snap, NULL);
        mutation = make_page(3, 0, removal ? 62 : 66, removal ? 0 : -1);
        if (removal) {
            for (int i = 6; i < 16; i++) {
                snprintf(mutation.items[i].message_id, sizeof(mutation.items[i].message_id), "m%d", i + 1);
                snprintf(mutation.items[i].sender_name, sizeof(mutation.items[i].sender_name), "Sender %d", i + 1);
            }
        }
        submit(mutation_req, mutation, false);
        assert(lab02_session_snapshot().pending_inbox_revision == 3);
        lab02_carousel_set_focus(&carousel, 6, true);
        assert(lab02_session_snapshot().inbox_revision == 3);
        assert(lab02_session_snapshot().focus_index == (removal ? 6 : 7));
        item = lab02_page_provider_item(&carousel.provider, carousel.focus_index);
        assert(item && !strcmp(item->message_id, removal ? "m7" : "m6"));
        assert_archived_ignored(&archived);
        advance(300); checkpoint("mutation-replacement-terminal");
        assert(lv_obj_get_scroll_x(carousel.scroll) == lab02_snap_target_x_for_index(removal ? 6 : 7, carousel.item_count));
        assert(snap_starts == snap_ends);

        assert(lab02_session_intent_refresh());
        mutation_req = take_page(0);
        lab02_carousel_set_focus(&carousel, 10, true);
        advance(40);
        submit(mutation_req, make_page(4, 0, 64, 0), false);
        assert(lab02_session_snapshot().pending_inbox_revision == 4);
        drag_to(2);
        lv_obj_send_event(carousel.scroll, LV_EVENT_SCROLL_END, NULL);
        advance(300); checkpoint("mutation-reversal-terminal");
        assert(lab02_session_snapshot().inbox_revision == 4);
        assert(lab02_session_snapshot().focus_index == 2 && snap_starts == snap_ends);

    } else if (!strcmp(scenario, "queue-full")) {
        lab02_fetch_req_t accepted = take_page(1);
        for (int i = 0; i < 8; i++) lab02_session_intent_transport((i & 1) != 0);
        lab02_inbox_page_t page = make_page(1, 16, 64, 0);
        /* No pumping between transport burst and the actual terminal producer path. */
        lab02_session_http_fetch_finished(LAB02_FETCH_OK, accepted.session_id,
            accepted.op_id, accepted.page_index, &page, false);
        printf("C0_TRACE scenario=%s event=accepted_terminal_retained op_id=%u dropped=%u\n",
               scenario, accepted.op_id, lab02_session_metrics().fetch_event_dropped);
        assert(lab02_session_metrics().fetch_event_dropped == 0);
        for (int i = 0; i < 12; i++) lab02_carousel_pump(&carousel);
        checkpoint("terminal-retained");
        assert(lab02_page_provider_item(&carousel.provider, 20));
    } else if (!strcmp(scenario, "stale-after-revision")) {
        lab02_fetch_req_t old = take_page(1);
        lab02_carousel_set_focus(&carousel, 8, false);
        assert(lab02_session_intent_refresh());
        submit(take_page(0), make_page(2, 0, 64, 0), false);
        checkpoint("revision2");
        submit(old, make_page(1, 16, 64, 0), false);
        checkpoint("old-result");
        assert(lab02_session_metrics().apply_stale == 1);
        assert(!lab02_page_provider_item(&carousel.provider, 20));
    } else { assert(!"unknown scenario"); }
    printf("C0_RESULT scenario=%s characterization=PASS acceptance=F001-C2\n", scenario);
    return 0;
}
