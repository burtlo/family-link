#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"

#include "lab02/trace_emit.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <string.h>
#include <stdio.h>

typedef enum {
    LAB02_SESS_EV_FETCH_DONE,
    LAB02_SESS_EV_STAGE,
    LAB02_SESS_EV_TRANSPORT,
} lab02_sess_ev_kind_t;

typedef struct {
    lab02_sess_ev_kind_t kind;
    lab02_fetch_outcome_t outcome;
    uint32_t session_id;
    uint32_t op_id;
    int page_index;
    bool initial;
    bool has_page;
    lab02_inbox_page_t page;
    lab02_load_stage_t stage;
    bool transport_reachable;
} lab02_sess_event_t;

static lab02_inbox_model_t s_model;
static lab02_page_provider_t s_provider;
static lab02_page_metadata_t s_meta;
static bool s_http_mode;
static int s_focus_index;
static char s_focus_message_id[sizeof(((lab02_inbox_item_t *)0)->message_id)];
static uint32_t s_inbox_revision;
static int64_t s_last_refresh_us;
static lab02_load_stage_t s_load_stage;
static bool s_scroll_locked;
static bool s_presentation_dirty;
static uint32_t s_next_op_id = 1;
static uint32_t s_session_generation;
static uint32_t s_inflight_op[LAB02_HTTP_MAX_PAGES];
static uint32_t s_loaded_pages;
static uint32_t s_failed_pages;
static bool s_refresh_failed;
static bool s_transport_unreachable;
static int64_t s_next_recovery_us;
static uint32_t s_deferred_op;

static lab02_session_metrics_t s_metrics;

static QueueHandle_t s_fetch_q;
static QueueHandle_t s_event_q;
static SemaphoreHandle_t s_fetch_slots;

static bool enqueue_fetch(int page_index, uint32_t *op_id_out, bool refresh);

static uint16_t effective_page_size(void)
{
    if (s_model.page_size > 0) {
        return s_model.page_size;
    }
    return LAB02_DEFAULT_PAGE_SIZE;
}

static bool post_session_event(const lab02_sess_event_t *ev)
{
    if (!s_event_q || !ev) {
        return false;
    }
    if (xQueueSend(s_event_q, ev, 0) != pdTRUE) {
        s_metrics.fetch_event_dropped++;
        lab02_trace_emit("fetch_event_dropped", 0, 0, NULL, NULL);
        return false;
    }
    return true;
}

static void release_fetch_slot(bool initial)
{
    if (!initial && s_fetch_slots) {
        xSemaphoreGive(s_fetch_slots);
    }
}

static bool stage_allowed(lab02_load_stage_t from, lab02_load_stage_t to)
{
    if (to == LAB02_LOAD_CONNECTING) {
        return from == LAB02_LOAD_SHELL;
    }
    if (to == LAB02_LOAD_FETCHING) {
        return from == LAB02_LOAD_CONNECTING;
    }
    if (to == LAB02_LOAD_READY) {
        return from == LAB02_LOAD_FETCHING;
    }
    if (to == LAB02_LOAD_ERROR) {
        return from == LAB02_LOAD_CONNECTING || from == LAB02_LOAD_FETCHING ||
               from == LAB02_LOAD_READY;
    }
    return false;
}

static void transition_stage(lab02_load_stage_t to)
{
    if (!stage_allowed(s_load_stage, to)) {
        s_metrics.stage_rejected++;
        lab02_trace_emit("stage_rejected", 0, (int)to, NULL, NULL);
        return;
    }
    s_load_stage = to;
    s_presentation_dirty = true;
}

static const lab02_inbox_model_t *active_model(void)
{
    if (s_provider.model) {
        return s_provider.model;
    }
    return &s_model;
}

static void rebuild_meta(void)
{
    lab02_page_metadata_build(&s_model, &s_meta);
    s_provider.meta = s_meta;
}

static uint16_t materialized_count(const lab02_inbox_model_t *m)
{
    if (!m) {
        return 0;
    }
    uint16_t n = 0;
    for (uint16_t i = 0; i < m->count && i < LAB02_INBOX_MAX_ITEMS; i++) {
        if (m->items[i].message_id[0] != 0) {
            n++;
        }
    }
    return n;
}

static int page_for_focus(int focus, uint16_t page_size)
{
    if (page_size == 0) {
        page_size = LAB02_DEFAULT_PAGE_SIZE;
    }
    return focus / (int)page_size;
}

static void prefetch_for_focus(int focus)
{
    if (!s_http_mode || s_load_stage != LAB02_LOAD_READY) {
        return;
    }
    const lab02_inbox_model_t *m = active_model();
    if (!m || m->count == 0) {
        return;
    }
    int page = page_for_focus(focus, effective_page_size());
    uint32_t op = 0;
    if (page > 0) {
        (void)enqueue_fetch(page - 1, &op, false);
    }
    (void)lab02_session_http_enqueue_fetch(page, &op);
    (void)lab02_session_http_enqueue_fetch(page + 1, &op);
}

static void reconcile_focus_id(void)
{
    const lab02_inbox_item_t *current = lab02_inbox_model_item(&s_model, s_focus_index);
    if (!s_focus_message_id[0]) {
        if (current) {
            snprintf(s_focus_message_id, sizeof(s_focus_message_id), "%s", current->message_id);
        }
        return;
    }
    for (uint16_t i = 0; i < s_model.count; i++) {
        if (strcmp(s_model.items[i].message_id, s_focus_message_id) == 0) {
            if (s_focus_index != (int)i) {
                s_focus_index = (int)i;
                s_presentation_dirty = true;
            }
            return;
        }
    }
    uint16_t psz = effective_page_size();
    int page = page_for_focus(s_focus_index, psz);
    int last_page = s_model.count > 0 ? (s_model.count - 1) / (int)psz : 0;
    bool neighbors_loaded = (s_loaded_pages & (1U << page)) != 0 &&
        (page == 0 || (s_loaded_pages & (1U << (page - 1))) != 0) &&
        (page >= last_page || (s_loaded_pages & (1U << (page + 1))) != 0);
    if (neighbors_loaded && current) {
        snprintf(s_focus_message_id, sizeof(s_focus_message_id), "%s", current->message_id);
    }
}

static bool apply_page(const lab02_sess_event_t *ev)
{
    int page = ev->page_index;
    const lab02_inbox_page_t *page_data = ev->has_page ? &ev->page : NULL;
    if (!ev->initial && page_data && s_inbox_revision != 0 &&
        page_data->revision < s_inbox_revision) {
        s_inflight_op[page] = 0;
        s_metrics.apply_stale++;
        lab02_trace_emit("apply_stale", ev->op_id, page, NULL, "old_revision");
        return false;
    }
    if (!ev->initial && page_data && s_inbox_revision != 0 &&
        page_data->revision > s_inbox_revision && page != 0) {
        s_inflight_op[page] = 0;
        s_metrics.apply_stale++;
        lab02_trace_emit("apply_stale", ev->op_id, page, NULL, "new_revision_needs_page0");
        uint32_t refresh_op = 0;
        (void)enqueue_fetch(0, &refresh_op, true);
        return false;
    }
    if (!ev->initial && page_data && page == 0 && s_inbox_revision != 0 &&
        page_data->revision != s_inbox_revision) {
        for (int p = 1; p < LAB02_HTTP_MAX_PAGES; p++) {
            s_inflight_op[p] = 0;
        }
        memset(&s_model, 0, sizeof(s_model));
        s_model.page_size = effective_page_size();
        s_loaded_pages = 0;
    }
    if (!page_data || !lab02_inbox_model_apply_v2_page(&s_model, page_data)) {
        if (!ev->initial) {
            s_inflight_op[page] = 0;
            s_failed_pages |= (1U << page);
            s_presentation_dirty = true;
        }
        s_metrics.fetch_fail++;
        if (page == 0 && !ev->initial) {
            s_refresh_failed = true;
        }
        lab02_trace_emit("apply_fail", ev->op_id, page, NULL, NULL);
        if (ev->initial) {
            transition_stage(LAB02_LOAD_ERROR);
        }
        return false;
    }
    s_inbox_revision = page_data->revision;
    if (page == 0) {
        s_refresh_failed = false;
    }
    s_failed_pages &= ~(1U << page);
    if (ev->initial) {
        transition_stage(LAB02_LOAD_READY);
        s_loaded_pages = 1U;
        s_last_refresh_us = esp_timer_get_time();
    } else if (page >= 0 && page < LAB02_HTTP_MAX_PAGES) {
        s_loaded_pages |= (1U << page);
        s_inflight_op[page] = 0;
    }
    rebuild_meta();
    if (s_focus_index >= (int)s_model.count && s_model.count > 0) {
        s_focus_index = (int)s_model.count - 1;
    }
    if (s_model.count == 0) {
        s_focus_index = 0;
    }
    reconcile_focus_id();
    s_presentation_dirty = true;
    s_metrics.apply_ok++;
    lab02_trace_emit("page_apply", ev->op_id, s_focus_index, NULL, NULL);
    if (ev->initial) {
        prefetch_for_focus(s_focus_index);
    } else if (page == 0) {
        prefetch_for_focus(s_focus_index);
    }
    return true;
}

static void handle_fetch_done(const lab02_sess_event_t *ev)
{
    if (!ev) {
        return;
    }
    int page = ev->page_index;
    if (page < 0 || page >= LAB02_HTTP_MAX_PAGES) {
        return;
    }
    if (!ev->initial && s_inflight_op[page] != ev->op_id) {
        s_metrics.apply_stale++;
        lab02_trace_emit("apply_stale", 0, page, NULL, "op_mismatch");
        return;
    }
    if (ev->outcome == LAB02_FETCH_FAIL) {
        s_metrics.fetch_fail++;
        s_inflight_op[page] = 0;
        s_failed_pages |= (1U << page);
        if (page == 0 && !ev->initial) {
            s_refresh_failed = true;
        }
        s_presentation_dirty = true;
        if (ev->initial) {
            transition_stage(LAB02_LOAD_ERROR);
        }
        lab02_trace_emit("fetch_fail", ev->op_id, page, NULL, NULL);
        return;
    }
    if (ev->outcome == LAB02_FETCH_TIMEOUT) {
        s_metrics.fetch_timeout++;
        s_inflight_op[page] = 0;
        s_failed_pages |= (1U << page);
        if (page == 0 && !ev->initial) {
            s_refresh_failed = true;
        }
        s_presentation_dirty = true;
        if (ev->initial) {
            transition_stage(LAB02_LOAD_ERROR);
        }
        lab02_trace_emit("fetch_timeout", ev->op_id, page, NULL, NULL);
        return;
    }
    apply_page(ev);
}

void lab02_session_reset(void)
{
    s_session_generation++;
    if (s_session_generation == 0) {
        s_session_generation = 1;
    }
    memset(&s_model, 0, sizeof(s_model));
    memset(&s_provider, 0, sizeof(s_provider));
    memset(&s_meta, 0, sizeof(s_meta));
    s_http_mode = false;
    s_focus_index = 0;
    s_focus_message_id[0] = 0;
    s_inbox_revision = 0;
    s_last_refresh_us = 0;
    s_load_stage = LAB02_LOAD_SHELL;
    s_scroll_locked = false;
    s_presentation_dirty = false;
    memset(s_inflight_op, 0, sizeof(s_inflight_op));
    s_loaded_pages = 0;
    s_failed_pages = 0;
    s_refresh_failed = false;
    s_transport_unreachable = false;
    s_next_recovery_us = 0;
    s_deferred_op = 0;
    memset(&s_metrics, 0, sizeof(s_metrics));
}

void lab02_session_attach_local(lab02_page_provider_t *pp)
{
    lab02_session_reset();
    if (!pp || !pp->model) {
        return;
    }
    s_provider = *pp;
    s_load_stage = LAB02_LOAD_READY;
    s_focus_index = 0;
    s_presentation_dirty = true;
}

bool lab02_session_begin_http(lab02_page_provider_t *pp)
{
    lab02_session_reset();
    if (!pp) {
        return false;
    }
    s_http_mode = true;
    s_model.page_size = LAB02_DEFAULT_PAGE_SIZE;
    s_provider.model = &s_model;
    rebuild_meta();
    *pp = s_provider;
    s_load_stage = LAB02_LOAD_SHELL;
    if (!s_fetch_q) {
        s_fetch_q = xQueueCreate(LAB02_FETCH_QUEUE_CAP, sizeof(lab02_fetch_req_t));
    }
    if (!s_event_q) {
        s_event_q = xQueueCreate(8, sizeof(lab02_sess_event_t));
    }
    if (!s_fetch_slots) {
        s_fetch_slots = xSemaphoreCreateCounting(LAB02_FETCH_QUEUE_CAP, LAB02_FETCH_QUEUE_CAP);
    }
    return s_fetch_q && s_event_q && s_fetch_slots;
}

lab02_session_snapshot_t lab02_session_snapshot(void)
{
    const lab02_inbox_model_t *m = active_model();
    lab02_session_snapshot_t snap = {
        .focus_index = s_focus_index,
        .total_count = m ? m->count : 0,
        .inbox_revision = s_inbox_revision,
        .load_stage = s_load_stage,
        .scroll_locked = s_scroll_locked,
        .presentation_dirty = s_presentation_dirty,
        .http_active = s_http_mode,
        .focused_page_failed = s_http_mode &&
            ((s_failed_pages & (1U << page_for_focus(s_focus_index, effective_page_size()))) != 0),
        .cached_due_to_refresh_failure = s_http_mode && s_refresh_failed,
        .transport_unreachable = s_http_mode && s_transport_unreachable,
    };
    return snap;
}

uint16_t lab02_session_http_page_size(void)
{
    return effective_page_size();
}

lab02_session_metrics_t lab02_session_metrics(void)
{
    return s_metrics;
}

const lab02_page_provider_t *lab02_session_provider(void)
{
    return &s_provider;
}

void lab02_session_intent_focus(int focus_index)
{
    if (focus_index < 0) {
        focus_index = 0;
    }
    const lab02_inbox_model_t *m = active_model();
    if (m && m->count > 0 && focus_index >= (int)m->count) {
        focus_index = (int)m->count - 1;
    }
    if (focus_index == s_focus_index) {
        const lab02_inbox_item_t *focused = lab02_inbox_model_item(m, focus_index);
        if (focused) {
            snprintf(s_focus_message_id, sizeof(s_focus_message_id), "%s", focused->message_id);
        }
        prefetch_for_focus(s_focus_index);
        return;
    }
    s_focus_index = focus_index;
    const lab02_inbox_item_t *focused = lab02_inbox_model_item(m, focus_index);
    if (focused) {
        snprintf(s_focus_message_id, sizeof(s_focus_message_id), "%s", focused->message_id);
    } else {
        s_focus_message_id[0] = 0;
    }
    s_presentation_dirty = true;
    prefetch_for_focus(s_focus_index);
}

void lab02_session_intent_scroll_lock(bool locked)
{
    bool was_locked = s_scroll_locked;
    s_scroll_locked = locked;
    if (was_locked && !locked) {
        s_deferred_op = 0;
        lab02_session_pump();
    }
}

static void apply_transport_reachable(bool reachable)
{
    bool unreachable = !reachable;
    if (s_transport_unreachable == unreachable) {
        return;
    }
    s_transport_unreachable = unreachable;
    s_presentation_dirty = true;
}

void lab02_session_intent_transport(bool reachable)
{
    lab02_sess_event_t ev = {
        .kind = LAB02_SESS_EV_TRANSPORT,
        .transport_reachable = reachable,
        .session_id = s_session_generation,
    };
    (void)post_session_event(&ev);
}

bool lab02_session_intent_refresh(void)
{
    if (!s_http_mode || s_load_stage != LAB02_LOAD_READY || s_scroll_locked) {
        return false;
    }
    uint32_t op = 0;
    bool queued = enqueue_fetch(0, &op, true);
    if (queued) {
        s_last_refresh_us = esp_timer_get_time();
    }
    return queued;
}

void lab02_session_pump(void)
{
    lab02_sess_event_t ev;
    int completions = 0;
    while (s_event_q && xQueuePeek(s_event_q, &ev, 0) == pdTRUE) {
        if (ev.session_id != s_session_generation) {
            xQueueReceive(s_event_q, &ev, 0);
            if (ev.kind == LAB02_SESS_EV_FETCH_DONE && !ev.initial) {
                int page = ev.page_index;
                if (page >= 0 && page < LAB02_HTTP_MAX_PAGES &&
                    s_inflight_op[page] == ev.op_id) {
                    s_inflight_op[page] = 0;
                }
            }
            continue;
        }
        if (ev.kind == LAB02_SESS_EV_FETCH_DONE && ev.outcome == LAB02_FETCH_OK &&
            s_scroll_locked) {
            if (s_deferred_op != ev.op_id) {
                s_deferred_op = ev.op_id;
                s_metrics.apply_deferred++;
                lab02_trace_emit("apply_deferred", ev.op_id, ev.page_index, NULL, NULL);
            }
            break;
        }
        xQueueReceive(s_event_q, &ev, 0);
        if (ev.kind == LAB02_SESS_EV_FETCH_DONE) {
            handle_fetch_done(&ev);
            completions++;
        } else if (ev.kind == LAB02_SESS_EV_STAGE) {
            transition_stage(ev.stage);
        } else if (ev.kind == LAB02_SESS_EV_TRANSPORT) {
            apply_transport_reachable(ev.transport_reachable);
        }
        if (completions >= 1) {
            break;
        }
    }
    if (s_http_mode && s_load_stage == LAB02_LOAD_READY && !s_scroll_locked) {
        int64_t now = esp_timer_get_time();
        if (!lab02_inbox_model_item(&s_model, s_focus_index) &&
            now >= s_next_recovery_us) {
            uint32_t op = 0;
            int page = page_for_focus(s_focus_index, effective_page_size());
            if (enqueue_fetch(page, &op, false)) {
                s_metrics.fetch_retry++;
            }
            s_next_recovery_us = now + 1000000;
        }
        if (s_last_refresh_us == 0 || now - s_last_refresh_us >= 5000000) {
            bool active = false;
            for (int p = 0; p < LAB02_HTTP_MAX_PAGES; p++) {
                active |= s_inflight_op[p] != 0;
            }
            if (!active) {
                (void)lab02_session_intent_refresh();
            }
        }
    }
}

bool lab02_session_take_presentation_dirty(void)
{
    bool dirty = s_presentation_dirty;
    s_presentation_dirty = false;
    return dirty;
}

void lab02_session_start_http_io(void)
{
    if (!s_http_mode) {
        return;
    }
    extern void lab02_page_provider_http_start_io(void);
    lab02_page_provider_http_start_io();
}

bool lab02_session_http_enqueue_fetch(int page_index, uint32_t *op_id_out)
{
    return enqueue_fetch(page_index, op_id_out, false);
}

static bool enqueue_fetch(int page_index, uint32_t *op_id_out, bool refresh)
{
    if (!s_http_mode || !s_fetch_q || page_index < 0 || page_index >= LAB02_HTTP_MAX_PAGES) {
        return false;
    }
    uint16_t psz = effective_page_size();
    if (!refresh && s_model.count > 0 && page_index * psz >= s_model.count) {
        return false;
    }
    if (!refresh && (s_loaded_pages & (1U << page_index))) {
        return false;
    }
    if (s_inflight_op[page_index] != 0) {
        return false;
    }
    if (xSemaphoreTake(s_fetch_slots, 0) != pdTRUE) {
        s_metrics.fetch_queue_overflow++;
        lab02_trace_emit("fetch_queue_full", 0, page_index, NULL, NULL);
        return false;
    }
    uint32_t op_id = s_next_op_id++;
    s_inflight_op[page_index] = op_id;
    s_failed_pages &= ~(1U << page_index);
    lab02_fetch_req_t req = {
        .page_index = page_index,
        .op_id = op_id,
        .session_id = s_session_generation,
        .submitted_us = esp_timer_get_time(),
    };
    if (xQueueSend(s_fetch_q, &req, 0) != pdTRUE) {
        s_inflight_op[page_index] = 0;
        xSemaphoreGive(s_fetch_slots);
        s_metrics.fetch_queue_overflow++;
        lab02_trace_emit("fetch_queue_full", 0, page_index, NULL, NULL);
        return false;
    }
    if (op_id_out) {
        *op_id_out = op_id;
    }
    return true;
}

void lab02_session_http_fetch_finished(lab02_fetch_outcome_t outcome, uint32_t session_id,
                                       uint32_t op_id,
                                       int page_index, const lab02_inbox_page_t *page, bool initial)
{
    lab02_sess_event_t ev = {
        .kind = LAB02_SESS_EV_FETCH_DONE,
        .outcome = outcome,
        .session_id = session_id,
        .op_id = op_id,
        .page_index = page_index,
        .initial = initial,
        .has_page = page != NULL && outcome == LAB02_FETCH_OK,
    };
    if (ev.has_page) {
        ev.page = *page;
    }
    if (!post_session_event(&ev)) {
        if (!initial && page_index >= 0 && page_index < LAB02_HTTP_MAX_PAGES) {
            s_inflight_op[page_index] = 0;
        }
        release_fetch_slot(initial);
        return;
    }
    release_fetch_slot(initial);
}

bool lab02_session_http_take_fetch_request(lab02_fetch_req_t *out)
{
    if (!s_fetch_q || !out) {
        return false;
    }
    return xQueueReceive(s_fetch_q, out, portMAX_DELAY) == pdTRUE;
}

void lab02_session_http_mark_stage(lab02_load_stage_t stage, uint32_t session_id)
{
    lab02_sess_event_t ev = {
        .kind = LAB02_SESS_EV_STAGE,
        .stage = stage,
        .session_id = session_id,
    };
    (void)post_session_event(&ev);
}

uint32_t lab02_session_generation(void)
{
    return s_session_generation;
}

uint16_t lab02_session_materialized_count(void)
{
    return materialized_count(active_model());
}
