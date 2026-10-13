#pragma once

#include "lab02/inbox_model.h"
#include "lab02/inbox_page_metadata.h"
#include "lab02/page_provider.h"

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAB02_FETCH_QUEUE_CAP 8
#define LAB02_PAGE_JSON_MAX 16384
#define LAB02_HTTP_MAX_PAGES 32

typedef enum {
    LAB02_LOAD_SHELL,
    LAB02_LOAD_CONNECTING,
    LAB02_LOAD_FETCHING,
    LAB02_LOAD_READY,
    LAB02_LOAD_ERROR,
} lab02_load_stage_t;

typedef struct {
    int focus_index;
    uint16_t total_count;
    uint32_t inbox_revision; /* Applied model revision. */
    uint32_t pending_inbox_revision; /* Accepted mutation; not yet visible. */
    lab02_load_stage_t load_stage;
    bool scroll_locked;
    bool presentation_dirty;
    bool http_active;
    bool focused_page_failed;
    bool identity_resolving; /* Stable viewed ID awaits bounded revision search. */
    bool cached_due_to_refresh_failure;
    bool transport_unreachable;
} lab02_session_snapshot_t;

typedef struct {
    uint32_t fetch_queue_overflow;
    uint32_t fetch_event_dropped;
    uint32_t fetch_fail;
    uint32_t fetch_timeout;
    uint32_t apply_deferred;
    uint32_t apply_stale;
    uint32_t apply_ok;
    uint32_t fetch_retry;
    uint32_t stage_rejected;
} lab02_session_metrics_t;

void lab02_session_reset(void);

/** Local fixture path: model owned elsewhere; session mirrors focus and metadata. */
void lab02_session_attach_local(lab02_page_provider_t *pp);

/** HTTP path: empty shell; worker fills model via session events. */
bool lab02_session_begin_http(lab02_page_provider_t *pp);

typedef enum {
    LAB02_SLOT_OUT_OF_RANGE,
    LAB02_SLOT_LOADING,
    LAB02_SLOT_BOUND,
    LAB02_SLOT_ERROR,
} lab02_slot_state_t;

typedef struct {
    int index;
    uint32_t session_generation;
    uint32_t inbox_revision;
    lab02_slot_state_t state;
    const lab02_inbox_item_t *item; /* Owner-thread snapshot; consume before next pump. */
} lab02_slot_snapshot_t;

/** Coherent owner-thread page/content state; presentation never infers failure. */
lab02_slot_snapshot_t lab02_session_slot_snapshot(const lab02_page_provider_t *provider, int index);

lab02_session_snapshot_t lab02_session_snapshot(void);
lab02_session_metrics_t lab02_session_metrics(void);
const lab02_page_provider_t *lab02_session_provider(void);

void lab02_session_intent_focus(int focus_index);
void lab02_session_intent_scroll_lock(bool locked);
/** Wi-Fi / IP reachability (Phase B); posts session event — apply in pump only. */
void lab02_session_intent_transport(bool reachable);
bool lab02_session_intent_refresh(void);
/** Owner tap retry for a terminal failed page (nonblocking; coalesces rapid repeats). */
bool lab02_session_intent_retry_page_for_index(int index);
/** Owner tap retry in ERROR: retain one intent; pump admission and lifecycle dispatch are deferred. */
bool lab02_session_intent_retry_initial_load(void);

/** Page stride for HTTP cursor math and paging (model page_size or default). */
uint16_t lab02_session_http_page_size(void);

/**
 * Accept at most one worker completion or apply one deferred mutation per pump. Call from presentation when LVGL is
 * safe to mutate (not from HTTP worker).
 */
void lab02_session_pump(void);
bool lab02_session_take_presentation_dirty(void);

/** Start Wi-Fi + initial page fetch (non-blocking; call after LVGL unlock). */
void lab02_session_start_http_io(void);

typedef enum {
    LAB02_FETCH_OK,
    LAB02_FETCH_FAIL,
    LAB02_FETCH_TIMEOUT,
} lab02_fetch_outcome_t;

/** Session owner: reserve terminal capacity and enqueue a frozen page request. */
bool lab02_session_http_enqueue_fetch(int page_index, uint32_t *op_id_out);

/** HTTP worker: fill admitted terminal mailbox; duplicates cannot overwrite it. */
void lab02_session_http_fetch_finished(lab02_fetch_outcome_t outcome, uint32_t session_id,
                                       uint32_t op_id,
                                       int page_index, const lab02_inbox_page_t *page, bool initial);

#ifdef __cplusplus
}
#endif
