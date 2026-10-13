#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "lab02/page_provider_http.h"
#include "lab02/snap_id.h"

#include "lab02/trace_emit.h"

#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/semphr.h"

#include <stdatomic.h>
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
    lab02_fetch_req_t request;
} lab02_sess_event_t;

static lab02_inbox_model_t s_model;
static lab02_page_provider_t s_provider;
static lab02_page_metadata_t s_meta;
static bool s_http_mode;
static bool s_worker_available;
static int s_focus_index;
static char s_focus_message_id[sizeof(((lab02_inbox_item_t *)0)->message_id)];
static uint32_t s_inbox_revision;
static int64_t s_last_refresh_us;
static lab02_load_stage_t s_load_stage;
static bool s_scroll_locked;
static bool s_presentation_dirty;
static uint32_t s_next_op_id = 1;
static atomic_uint s_session_generation;
static uint32_t s_inflight_op[LAB02_HTTP_MAX_PAGES];
static uint32_t s_loaded_pages;
static uint32_t s_failed_pages;
static bool s_refresh_failed;
static bool s_transport_unreachable;
static int s_pending_focus_page = -1;
static uint8_t s_page_attempt[LAB02_HTTP_MAX_PAGES];
static int s_coalesce_retry_page = -1;
static int64_t s_coalesce_retry_until_us;
static int64_t s_focus_admission_us;
/* One immutable owner-held mutation page; never a borrowed mailbox pointer.
 * Accepted fetches release their reservation before visual reconciliation. */
static lab02_sess_event_t s_pending_revision;
static bool s_has_pending_revision;
/* One presentation-owner retry intent; reset supersedes an unadmitted tap. */
static bool s_initial_retry_pending;
/* Delivery mutex protects the immutable admitted restart command. */
static bool s_initial_restart_pending;

static lab02_session_metrics_t s_metrics;

/* Eight normal admissions and one dedicated boot admission. A reservation is
 * retained through fetch acceptance. Deferred mutation has separate storage. Producers
 * only write this delivery bookkeeping, never session/domain state. */
#define TERMINAL_CAP (LAB02_FETCH_QUEUE_CAP + 1)
typedef struct {
    bool admitted;
    bool posted;
    uint64_t sequence;
    lab02_sess_event_t event;
} terminal_slot_t;
static terminal_slot_t s_terminals[TERMINAL_CAP];
static uint64_t s_terminal_sequence;
static int s_reconciled_slot = -1;
static QueueHandle_t s_fetch_q;
static SemaphoreHandle_t s_fetch_slots;
static SemaphoreHandle_t s_post_mutex;
static lab02_sess_event_t s_pump_ev;
static uint32_t s_stage_session;
static bool s_worker_status_pending;
static bool s_worker_status_available;
static bool s_worker_status_reported;
static uint32_t s_pending_stages;
static uint32_t s_transport_session;
static bool s_transport_pending;
static bool s_transport_latest;

static bool enqueue_fetch(int page_index, uint32_t *op_id_out, bool refresh);

static uint16_t effective_page_size(void)
{
    return s_model.page_size > 0 ? s_model.page_size : LAB02_DEFAULT_PAGE_SIZE;
}

static lab02_fetch_req_t request_envelope(int page_index, uint32_t op_id)
{
    int64_t now = esp_timer_get_time();
    uint8_t attempt = s_page_attempt[page_index];
    if (attempt == 0) {
        attempt = 1;
    }
    lab02_fetch_req_t req = {
        .page_index = page_index, .op_id = op_id,
        .session_id = s_session_generation, .revision = s_inbox_revision,
        .page_size = effective_page_size(),
        .page_offset = page_index * (int)effective_page_size(),
        .attempt = attempt, .submitted_us = now, .response_read_timeout_us = 12000000,
    };
    return req;
}

/* Presentation uses try-locks only; a busy producer leaves bounded work pending. */
static bool finish_reconciled_slot(void)
{
    if (s_reconciled_slot < 0) return true;
    if (xSemaphoreTake(s_post_mutex, 0) != pdTRUE) return false;
    int slot = s_reconciled_slot;
    memset(&s_terminals[slot], 0, sizeof(s_terminals[slot]));
    if (slot < LAB02_FETCH_QUEUE_CAP) xSemaphoreGive(s_fetch_slots);
    s_reconciled_slot = -1;
    xSemaphoreGive(s_post_mutex);
    return true;
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
        return from == LAB02_LOAD_SHELL || from == LAB02_LOAD_CONNECTING || from == LAB02_LOAD_FETCHING ||
               from == LAB02_LOAD_READY;
    }
    if (to == LAB02_LOAD_SHELL) {
        return from == LAB02_LOAD_ERROR;
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
    /* Focus admission precedes speculative neighbors. Retain only the latest
     * not-yet-admitted intent; a failed admitted operation is a separate policy. */
    s_pending_focus_page = -1;
    if (!lab02_session_http_enqueue_fetch(page, &op) &&
        !(s_loaded_pages & (1U << page)) && !s_inflight_op[page] &&
        !(s_failed_pages & (1U << page))) {
        s_pending_focus_page = page;
        s_focus_admission_us = esp_timer_get_time() + 20000;
        lab02_trace_emit("focus_admission_pending", 0, page, NULL, NULL);
    }
    if (page > 0) (void)enqueue_fetch(page - 1, &op, false);
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
        s_inflight_op[page] = 0;
        if (s_worker_available) {
            lab02_page_provider_http_notify_initial_ready(s_inbox_revision, s_model.count);
        } else {
            transition_stage(LAB02_LOAD_ERROR);
        }
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
    char apply_extra[240];
    snprintf(apply_extra, sizeof(apply_extra),
             "op_id=%u revision=%u snap_id=%u fetch_snap_id=%u page_receipt_id=%s run_id=%s server_completed_ns=%llu",
             (unsigned)ev->op_id, (unsigned)s_inbox_revision, (unsigned)lab02_snap_current_id(),
             (unsigned)ev->page.snap_id,
             ev->page.page_receipt_id[0] ? ev->page.page_receipt_id : "-",
             ev->page.run_id[0] ? ev->page.run_id : "-",
             (unsigned long long)ev->page.server_completed_ns);
    lab02_trace_emit("page_apply", ev->op_id, s_focus_index, s_focus_message_id, apply_extra);
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
    s_worker_available = false;
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
    s_pending_focus_page = -1;
    s_initial_retry_pending = false;
    memset(s_page_attempt, 0, sizeof(s_page_attempt));
    s_coalesce_retry_page = -1;
    s_coalesce_retry_until_us = 0;
    s_focus_admission_us = 0;
    if (s_has_pending_revision) {
        lab02_trace_emit("model_pending_stale", s_pending_revision.op_id, 0, NULL, "reason=session_superseded");
    }
    s_has_pending_revision = false;
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
    if (!s_fetch_slots) {
        s_fetch_slots = xSemaphoreCreateCounting(LAB02_FETCH_QUEUE_CAP, LAB02_FETCH_QUEUE_CAP);
    }
    if (!s_post_mutex) s_post_mutex = xSemaphoreCreateMutex();
    if (!s_fetch_q || !s_fetch_slots || !s_post_mutex ||
        xSemaphoreTake(s_post_mutex, 0) != pdTRUE) return false;
    terminal_slot_t *boot = &s_terminals[LAB02_FETCH_QUEUE_CAP];
    bool available = !boot->admitted;
    if (available) {
        memset(boot, 0, sizeof(*boot));
        boot->admitted = true;
        uint32_t boot_op = s_next_op_id++;
        boot->event.request = request_envelope(0, boot_op);
        s_page_attempt[0] = 1;
        boot->event.initial = true;
        s_stage_session = s_session_generation;
        s_pending_stages = 0;
        s_initial_restart_pending = false;
        s_worker_status_pending = false;
        s_worker_status_reported = false;
    }
    xSemaphoreGive(s_post_mutex);
    return available;
}

lab02_session_snapshot_t lab02_session_snapshot(void)
{
    const lab02_inbox_model_t *m = active_model();
    lab02_session_snapshot_t snap = {
        .focus_index = s_focus_index,
        .total_count = m ? m->count : 0,
        .inbox_revision = s_inbox_revision,
        .pending_inbox_revision = s_has_pending_revision ? s_pending_revision.page.revision : 0,
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

lab02_slot_snapshot_t lab02_session_slot_snapshot(const lab02_page_provider_t *provider, int index)
{
    lab02_slot_snapshot_t slot = {.index = index,
        .session_generation = s_session_generation, .inbox_revision = s_inbox_revision,
        .state = LAB02_SLOT_OUT_OF_RANGE};
    if (!provider || !provider->model || index < 0 || index >= provider->model->count) return slot;
    slot.item = lab02_inbox_model_item(provider->model, index);
    int page = page_for_focus(index, effective_page_size());
    if (slot.item) slot.state = LAB02_SLOT_BOUND;
    else if (s_http_mode && page < LAB02_HTTP_MAX_PAGES &&
             (s_failed_pages & (1U << page))) slot.state = LAB02_SLOT_ERROR;
    else slot.state = LAB02_SLOT_LOADING;
    return slot;
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
    if (!s_post_mutex || xSemaphoreTake(s_post_mutex, portMAX_DELAY) != pdTRUE) return;
    s_transport_session = s_stage_session;
    s_transport_latest = reachable;
    s_transport_pending = true;
    xSemaphoreGive(s_post_mutex);
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

static bool owner_retry_page(int page_index)
{
    if (!s_http_mode || s_load_stage != LAB02_LOAD_READY || page_index < 0 ||
        page_index >= LAB02_HTTP_MAX_PAGES) {
        return false;
    }
    if (!(s_failed_pages & (1U << page_index)) && s_inflight_op[page_index] == 0) {
        return false;
    }
    int64_t now = esp_timer_get_time();
    if (page_index == s_coalesce_retry_page && now < s_coalesce_retry_until_us) {
        return true;
    }
    if (s_inflight_op[page_index] != 0) {
        s_coalesce_retry_page = page_index;
        s_coalesce_retry_until_us = now + 20000;
        return true;
    }
    if (s_page_attempt[page_index] < 255) {
        s_page_attempt[page_index]++;
    }
    uint32_t op = 0;
    bool queued = enqueue_fetch(page_index, &op, false);
    if (queued) {
        s_metrics.fetch_retry++;
        s_coalesce_retry_page = page_index;
        s_coalesce_retry_until_us = now + 20000;
        char extra[96];
        snprintf(extra, sizeof(extra), "page=%d attempt=%u op_id=%u",
                 page_index, (unsigned)s_page_attempt[page_index], (unsigned)op);
        lab02_trace_emit("owner_retry_page", op, page_index, NULL, extra);
    }
    return queued;
}

bool lab02_session_intent_retry_page_for_index(int index)
{
    const lab02_inbox_model_t *m = active_model();
    if (!m || index < 0 || index >= (int)m->count) {
        return false;
    }
    return owner_retry_page(page_for_focus(index, effective_page_size()));
}

bool lab02_session_intent_retry_initial_load(void)
{
    if (!s_http_mode || s_load_stage != LAB02_LOAD_ERROR) return false;
    s_initial_retry_pending = true; /* repeated taps coalesce, even under contention */
    return true;
}

static void admit_initial_retry(void)
{
    if (!s_initial_retry_pending) return;
    if (!s_http_mode || s_load_stage != LAB02_LOAD_ERROR) {
        s_initial_retry_pending = false;
        return;
    }
    if (!s_post_mutex || xSemaphoreTake(s_post_mutex, 0) != pdTRUE) {
        return;
    }
    terminal_slot_t *boot = &s_terminals[LAB02_FETCH_QUEUE_CAP];
    if (!boot->admitted) {
        uint32_t boot_op = s_next_op_id++;
        memset(boot, 0, sizeof(*boot));
        boot->admitted = true;
        if (s_page_attempt[0] < 255) {
            s_page_attempt[0]++;
        } else {
            s_page_attempt[0] = 2;
        }
        boot->event.request = request_envelope(0, boot_op);
        boot->event.initial = true;
        s_worker_status_reported = false;
        s_worker_status_pending = false;
        s_worker_status_available = false;
        s_inflight_op[0] = 0;
        s_failed_pages &= ~(1U << 0);
        s_pending_stages |= 1U << LAB02_LOAD_SHELL;
        s_presentation_dirty = true;
        s_initial_retry_pending = false;
        s_initial_restart_pending = true;
        s_metrics.fetch_retry++;
        char extra[80];
        snprintf(extra, sizeof(extra), "attempt=%u op_id=%u", (unsigned)s_page_attempt[0],
                 (unsigned)boot_op);
        lab02_trace_emit("owner_retry_initial", boot_op, 0, NULL, extra);
    }
    xSemaphoreGive(s_post_mutex);
}

bool lab02_session_http_take_initial_restart(lab02_fetch_req_t *out)
{
    if (!out || !s_post_mutex || xSemaphoreTake(s_post_mutex, 0) != pdTRUE) return false;
    terminal_slot_t *boot = &s_terminals[LAB02_FETCH_QUEUE_CAP];
    bool available = s_initial_restart_pending && boot->admitted && !boot->posted &&
                     boot->event.request.session_id == atomic_load(&s_session_generation);
    if (available) *out = boot->event.request;
    s_initial_restart_pending = false;
    xSemaphoreGive(s_post_mutex);
    return available;
}

void lab02_session_pump(void)
{
    if (!s_post_mutex || !finish_reconciled_slot()) return;
    admit_initial_retry();
    if (xSemaphoreTake(s_post_mutex, 0) != pdTRUE) return;
    uint32_t stages = s_stage_session == s_session_generation ? s_pending_stages : 0;
    s_pending_stages = 0;
    bool worker_status = s_stage_session == s_session_generation && s_worker_status_pending;
    bool worker_available = s_worker_status_available;
    if (worker_status) s_worker_status_pending = false;
    bool transport = s_transport_pending && s_transport_session == s_session_generation;
    bool reachable = s_transport_latest;
    s_transport_pending = false;
    /* Owner cancellation terminalizes superseded admissions even when their
     * producer has not returned. Late producers then see posted/released state. */
    for (int i = 0; i < TERMINAL_CAP; i++) {
        terminal_slot_t *entry = &s_terminals[i];
        lab02_fetch_req_t *req = &entry->event.request;
        if (entry->admitted && !entry->posted &&
            (req->session_id != s_session_generation ||
             (!entry->event.initial &&
              (s_inflight_op[req->page_index] != req->op_id || req->revision != s_inbox_revision)))) {
            entry->event.session_id = req->session_id;
            entry->event.op_id = req->op_id;
            entry->event.page_index = req->page_index;
            entry->sequence = ++s_terminal_sequence;
            entry->posted = true;
        }
    }
    int slot = -1;
    for (int i = 0; i < TERMINAL_CAP && (s_scroll_locked || !s_has_pending_revision); i++) {
        if (s_terminals[i].posted &&
            (slot < 0 || s_terminals[i].sequence < s_terminals[slot].sequence)) slot = i;
    }
    if (slot >= 0) s_pump_ev = s_terminals[slot].event;
    xSemaphoreGive(s_post_mutex);
    if (worker_status) {
        s_worker_available = worker_available;
        char extra[120];
        snprintf(extra, sizeof(extra), "session=%u kind=http_startup outcome=%s reason=%s",
                 (unsigned)s_session_generation, worker_available ? "success" : "failure",
                 worker_available ? "worker_available" : "worker_unavailable");
        lab02_trace_emit("io_start_terminal", 0, 0, NULL, extra);
    }
    /* Preserve ordered startup transitions even if both were posted before a
     * pump. Repeated reports coalesce; invalid transitions remain observable. */
    for (int stage = LAB02_LOAD_SHELL; stage <= LAB02_LOAD_ERROR; stage++) {
        if (stages & (1U << stage)) transition_stage((lab02_load_stage_t)stage);
    }
    if (transport) apply_transport_reachable(reachable);
    if (slot >= 0) {
        bool stale = s_pump_ev.session_id != s_session_generation ||
            (!s_pump_ev.initial &&
             (s_inflight_op[s_pump_ev.page_index] != s_pump_ev.op_id ||
              s_pump_ev.request.revision != s_inbox_revision));
        if (stale) {
            s_metrics.apply_stale++;
            lab02_trace_emit("apply_stale", s_pump_ev.op_id, s_pump_ev.page_index, NULL, "session_revision_or_op_mismatch");
            if (s_pump_ev.session_id == s_session_generation &&
                s_inflight_op[s_pump_ev.page_index] == s_pump_ev.op_id)
                s_inflight_op[s_pump_ev.page_index] = 0;
        } else {
            uint32_t prior_stale = s_metrics.apply_stale;
            uint32_t prior_fail = s_metrics.fetch_fail;
            const lab02_inbox_page_t *page = &s_pump_ev.page;
            bool mutation = !s_pump_ev.initial && s_pump_ev.outcome == LAB02_FETCH_OK &&
                s_pump_ev.has_page && s_pump_ev.page_index == 0 &&
                page->revision > s_inbox_revision;
            bool valid = page->item_count <= LAB02_DEFAULT_PAGE_SIZE &&
                page->page_offset == 0 && page->total_count <= LAB02_INBOX_MAX_ITEMS;
            if (mutation && valid && (s_scroll_locked || s_has_pending_revision)) {
                s_inflight_op[0] = 0;
                if (s_has_pending_revision && page->revision < s_pending_revision.page.revision) {
                    s_metrics.apply_stale++;
                    lab02_trace_emit("apply_stale", s_pump_ev.op_id, 0, NULL, "reason=pending_newer_revision");
                } else {
                    if (s_has_pending_revision) {
                        lab02_trace_emit("model_pending_stale", s_pending_revision.op_id, 0, NULL,
                                         "reason=newer_revision_superseded");
                    }
                    s_pending_revision = s_pump_ev;
                    s_has_pending_revision = true;
                    s_metrics.apply_deferred++;
                    char pending_extra[96];
                    snprintf(pending_extra, sizeof(pending_extra),
                             "revision=%u applied_revision=%u reason=motion_reconciliation_pending",
                             (unsigned)page->revision, (unsigned)s_inbox_revision);
                    lab02_trace_emit("model_pending", s_pump_ev.op_id, 0, NULL, pending_extra);
                }
            } else {
                handle_fetch_done(&s_pump_ev);
            }
            stale = s_metrics.apply_stale != prior_stale;
            if (s_metrics.fetch_fail != prior_fail) s_pump_ev.outcome = LAB02_FETCH_FAIL;
        }
        char extra[160];
        snprintf(extra, sizeof(extra), "session=%u revision=%u attempt=%u outcome=%s elapsed_us=%lld",
                 (unsigned)s_pump_ev.session_id, (unsigned)s_pump_ev.request.revision,
                 (unsigned)s_pump_ev.request.attempt,
                 stale ? "stale" : (s_pump_ev.outcome == LAB02_FETCH_OK ? "success" :
                 (s_pump_ev.outcome == LAB02_FETCH_TIMEOUT ? "timed_out" : "failure")),
                 (long long)(esp_timer_get_time() - s_pump_ev.request.submitted_us));
        lab02_trace_emit("fetch_terminal", s_pump_ev.op_id, s_pump_ev.page_index, NULL, extra);
        s_reconciled_slot = slot;
        if (!finish_reconciled_slot()) return;
    }
    /* At most one completion acceptance OR one pending model apply per pump.
     * Reconcile against the latest owner focus/ID, never the arrival-time index. */
    if (slot < 0 && !s_scroll_locked && s_has_pending_revision) {
        if (s_pending_revision.session_id != s_session_generation ||
            s_pending_revision.page.revision <= s_inbox_revision) {
            lab02_trace_emit("model_pending_stale", s_pending_revision.op_id, 0, NULL,
                             "reason=session_or_revision_superseded");
        } else {
            (void)apply_page(&s_pending_revision);
        }
        s_has_pending_revision = false;
    }
    if (s_pending_focus_page >= 0 && s_load_stage == LAB02_LOAD_READY &&
        esp_timer_get_time() >= s_focus_admission_us) {
        int page = s_pending_focus_page;
        uint32_t op = 0;
        if (page != page_for_focus(s_focus_index, effective_page_size()) ||
            (s_loaded_pages & (1U << page)) || s_inflight_op[page] ||
            (s_failed_pages & (1U << page)) || enqueue_fetch(page, &op, false)) {
            s_pending_focus_page = -1;
        }
        s_focus_admission_us = esp_timer_get_time() + 20000;
    }
    if (s_http_mode && s_load_stage == LAB02_LOAD_READY && !s_scroll_locked) {
        int64_t now = esp_timer_get_time();
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
    if (!s_http_mode || !s_worker_available || !s_fetch_q || page_index < 0 || page_index >= LAB02_HTTP_MAX_PAGES) {
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
    if (xSemaphoreTake(s_post_mutex, 0) != pdTRUE) {
        xSemaphoreGive(s_fetch_slots);
        s_metrics.fetch_queue_overflow++;
        lab02_trace_emit("fetch_admission_busy", 0, page_index, NULL, NULL);
        return false;
    }
    int slot = 0;
    while (slot < LAB02_FETCH_QUEUE_CAP && s_terminals[slot].admitted) slot++;
    if (slot == LAB02_FETCH_QUEUE_CAP) {
        xSemaphoreGive(s_post_mutex);
        xSemaphoreGive(s_fetch_slots);
        return false;
    }
    uint32_t op_id = s_next_op_id++;
    if (!refresh && s_page_attempt[page_index] == 0) {
        s_page_attempt[page_index] = 1;
    }
    lab02_fetch_req_t req = request_envelope(page_index, op_id);
    terminal_slot_t *reservation = &s_terminals[slot];
    memset(reservation, 0, sizeof(*reservation));
    reservation->admitted = true;
    reservation->event.request = req;
    if (xQueueSend(s_fetch_q, &req, 0) != pdTRUE) {
        memset(reservation, 0, sizeof(*reservation));
        xSemaphoreGive(s_post_mutex);
        xSemaphoreGive(s_fetch_slots);
        s_metrics.fetch_queue_overflow++;
        lab02_trace_emit("fetch_queue_full", 0, page_index, NULL, NULL);
        return false;
    }
    s_inflight_op[page_index] = op_id;
    s_failed_pages &= ~(1U << page_index);
    xSemaphoreGive(s_post_mutex);
    lab02_trace_emit("fetch_admitted", op_id, page_index, NULL, NULL);
    if (op_id_out) {
        *op_id_out = op_id;
    }
    return true;
}

void lab02_session_http_fetch_finished(lab02_fetch_outcome_t outcome, uint32_t session_id,
                                       uint32_t op_id,
                                       int page_index, const lab02_inbox_page_t *page, bool initial)
{
    if (!s_post_mutex || xSemaphoreTake(s_post_mutex, portMAX_DELAY) != pdTRUE) return;
    terminal_slot_t *reservation = NULL;
    for (int i = 0; i < TERMINAL_CAP; i++) {
        terminal_slot_t *candidate = &s_terminals[i];
        lab02_fetch_req_t *req = &candidate->event.request;
        if (candidate->admitted && req->session_id == session_id && req->op_id == op_id &&
            req->page_index == page_index && candidate->event.initial == initial) {
            reservation = candidate;
            break;
        }
    }
    if (!reservation || reservation->posted) {
        xSemaphoreGive(s_post_mutex);
        lab02_trace_emit("fetch_delivery_rejected", op_id, page_index, NULL, "duplicate_or_unadmitted");
        return;
    }
    lab02_sess_event_t *ev = &reservation->event;
    ev->kind = LAB02_SESS_EV_FETCH_DONE;
    ev->outcome = outcome;
    ev->session_id = session_id;
    ev->op_id = op_id;
    ev->page_index = page_index;
    ev->has_page = page != NULL && outcome == LAB02_FETCH_OK;
    if (ev->has_page) ev->page = *page;
    reservation->sequence = ++s_terminal_sequence;
    reservation->posted = true;
    xSemaphoreGive(s_post_mutex);
    lab02_trace_emit("fetch_delivery_retained", op_id, page_index, NULL, NULL);
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
    if (!s_post_mutex || xSemaphoreTake(s_post_mutex, portMAX_DELAY) != pdTRUE) return;
    if (session_id == s_stage_session && stage >= LAB02_LOAD_SHELL && stage <= LAB02_LOAD_ERROR)
        s_pending_stages |= 1U << stage;
    xSemaphoreGive(s_post_mutex);
}

uint32_t lab02_session_generation(void)
{
    return s_session_generation;
}

uint16_t lab02_session_materialized_count(void)
{
    return materialized_count(active_model());
}

bool lab02_session_http_initial_request(lab02_fetch_req_t *out)
{
    if (!out || !s_post_mutex || xSemaphoreTake(s_post_mutex, 0) != pdTRUE) return false;
    terminal_slot_t *boot = &s_terminals[LAB02_FETCH_QUEUE_CAP];
    bool available = boot->admitted && !boot->posted &&
        boot->event.request.session_id == s_session_generation;
    if (available) *out = boot->event.request;
    xSemaphoreGive(s_post_mutex);
    return available;
}

void lab02_session_http_worker_started(bool available, uint32_t session_id)
{
    if (!s_post_mutex || xSemaphoreTake(s_post_mutex, portMAX_DELAY) != pdTRUE) return;
    /* Startup is one immutable result per session. Duplicate/stale reports
     * cannot reopen admission after a failed allocation. */
    if (session_id == s_stage_session && !s_worker_status_reported) {
        s_worker_status_reported = true;
        s_worker_status_available = available;
        s_worker_status_pending = true;
    }
    xSemaphoreGive(s_post_mutex);
}
