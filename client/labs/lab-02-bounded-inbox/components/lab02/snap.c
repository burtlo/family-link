#include "lab02/snap.h"
#include "lab02/instrumentation.h"
#include "lab02/trace_emit.h"
#include "lab02/lab02_session.h"
#include "lab02/lab02_session_io.h"
#include "esp_timer.h"
#include <stdatomic.h>
#include <stdint.h>
#include <string.h>

/* One presentation-owner envelope; no per-tick allocation. LVGL copies the
 * immutable ID into each animation. A recycled envelope is never authority. */
static struct {
    uint32_t id;
    uint32_t session_id;
    uint32_t revision;
    int64_t submitted_us;
    int32_t target;
    int focus;
    char message_id[24];
    char origin[LAB02_TRACE_TEXT_CAP];
    lv_obj_t *scroller;
    lab02_snap_state_t *state;
    lab02_snap_terminal_cb terminal;
    void *user;
    enum { SNAP_TERMINAL,
           SNAP_RUNNING } status;
} s_operation;
static atomic_uint_least32_t s_snap_id;

bool lab02_snap_is_current(uint32_t id)
{
    return s_operation.status == SNAP_RUNNING && s_operation.id == id;
}

static uint32_t animation_id(lv_anim_t *a)
{
    return (uint32_t)(uintptr_t)lv_anim_get_user_data(a);
}

static void finish(uint32_t id, lab02_snap_outcome_t outcome, const char *reason)
{
    if (!lab02_snap_is_current(id))
        return;
    int64_t terminal_us = esp_timer_get_time();
    s_operation.status = SNAP_TERMINAL; /* Exactly once, including reentrant deletion. */
    s_operation.state->scroll_lock = false;
    s_operation.state->driving_scroll = false;
    const char *name = outcome == LAB02_SNAP_SUCCESS ? "success" : outcome == LAB02_SNAP_STALE ? "stale"
                                                                                               : "cancelled";
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_SNAP_ID |
                  LAB02_TRACE_FIELD_OUTCOME | LAB02_TRACE_FIELD_REASON |
                  LAB02_TRACE_FIELD_BEFORE | LAB02_TRACE_FIELD_AFTER |
                  LAB02_TRACE_FIELD_REVISION | LAB02_TRACE_FIELD_ELAPSED_US |
                  LAB02_TRACE_FIELD_TARGET_X | LAB02_TRACE_FIELD_SUBMITTED_US |
                  LAB02_TRACE_FIELD_TERMINAL_US | LAB02_TRACE_FIELD_OP_KIND |
                  LAB02_TRACE_FIELD_RUN_ID | LAB02_TRACE_FIELD_SNAP_ORIGIN,
        .session_id = s_operation.session_id,
        .snap_id = id,
        .outcome = name,
        .reason = reason,
        .before = "running",
        .after = "terminal",
        .revision = s_operation.revision,
        .elapsed_us = terminal_us - s_operation.submitted_us,
        .submitted_us = s_operation.submitted_us,
        .terminal_us = terminal_us,
        .op_kind = "snap",
        .target_x = s_operation.target,
        .run_id = lab02_session_run_id() ? lab02_session_run_id() : "-",
        .snap_origin = s_operation.origin,
    };
    lab02_trace_emit_payload("snap_end", 0, s_operation.focus,
                             s_operation.message_id, &payload);
    if (outcome == LAB02_SNAP_SUCCESS)
        lab02_instrumentation_get()->snap_count++;
    lab02_snap_terminal_cb cb = s_operation.terminal;
    void *user = s_operation.user;
    if (cb)
        cb(user, id, outcome);
}

static void scroll_x_exec(lv_anim_t *a, int32_t value)
{
    if (!lab02_snap_is_current(animation_id(a)))
        return;
    if (s_operation.session_id != lab02_session_generation()) {
        finish(animation_id(a), LAB02_SNAP_STALE, "session_changed");
        return;
    }
    lab02_snap_state_t *st = s_operation.state;
    st->driving_scroll = true;
    lv_obj_scroll_to_x(s_operation.scroller, value, LV_ANIM_OFF);
    st->driving_scroll = false;
}

static void snap_anim_done(lv_anim_t *a)
{
    uint32_t id = animation_id(a);
    if (!lab02_snap_is_current(id))
        return;
    scroll_x_exec(a, s_operation.target);
    finish(id, LAB02_SNAP_SUCCESS, "completed");
}

static void snap_anim_deleted(lv_anim_t *a)
{
    finish(animation_id(a), LAB02_SNAP_CANCELLED, "animation_deleted");
}

void lab02_snap_cancel(lab02_snap_state_t *st, const char *reason)
{
    if (s_operation.status != SNAP_RUNNING || s_operation.state != st)
        return;
    lab02_snap_state_t *state = s_operation.state;
    finish(s_operation.id, LAB02_SNAP_STALE, reason);
    lv_anim_delete(state, NULL);
}

void lab02_snap_animate_to(lv_obj_t *scroller, lab02_snap_state_t *st, int32_t target_x,
                           int focus_index, const char *message_id,
                           const char *origin,
                           lab02_snap_terminal_cb on_done, void *user_data)
{
    if (!scroller || !st)
        return;
    if (s_operation.status == SNAP_RUNNING)
        lab02_snap_cancel(s_operation.state, "superseded");
    int32_t start = lv_obj_get_scroll_x(scroller);
    uint32_t id = atomic_fetch_add(&s_snap_id, 1) + 1;
    s_operation.id = id;
    /* Session generation is frozen telemetry, never callback authority. */
    s_operation.session_id = lab02_session_generation();
    s_operation.revision = lab02_session_snapshot().inbox_revision;
    s_operation.submitted_us = esp_timer_get_time();
    s_operation.target = target_x;
    s_operation.focus = focus_index;
    strncpy(s_operation.message_id, message_id ? message_id : "-",
            sizeof(s_operation.message_id) - 1u);
    s_operation.message_id[sizeof(s_operation.message_id) - 1u] = '\0';
    s_operation.scroller = scroller;
    s_operation.state = st;
    s_operation.terminal = on_done;
    s_operation.user = user_data;
    s_operation.status = SNAP_RUNNING;
    const char *selected_origin = (origin &&
                                   (!strcmp(origin, "touch") || !strcmp(origin, "harness") ||
                                    !strcmp(origin, "programmatic")))
                                      ? origin
                                      : "unknown";
    strncpy(s_operation.origin, selected_origin, sizeof(s_operation.origin) - 1u);
    s_operation.origin[sizeof(s_operation.origin) - 1u] = '\0';
    st->operation_id = id;
    st->snap_target_x = target_x;
    st->scroll_lock = true;
    const lab02_trace_payload_t payload = {
        .fields = LAB02_TRACE_FIELD_SESSION_ID | LAB02_TRACE_FIELD_SNAP_ID |
                  LAB02_TRACE_FIELD_BEFORE | LAB02_TRACE_FIELD_AFTER |
                  LAB02_TRACE_FIELD_REVISION | LAB02_TRACE_FIELD_START_X |
                  LAB02_TRACE_FIELD_TARGET_X | LAB02_TRACE_FIELD_SUBMITTED_US |
                  LAB02_TRACE_FIELD_OP_KIND | LAB02_TRACE_FIELD_RUN_ID |
                  LAB02_TRACE_FIELD_SNAP_ORIGIN,
        .session_id = s_operation.session_id,
        .snap_id = id,
        .before = "pending",
        .after = "running",
        .revision = s_operation.revision,
        .start_x = start,
        .target_x = target_x,
        .submitted_us = s_operation.submitted_us,
        .op_kind = "snap",
        .run_id = lab02_session_run_id() ? lab02_session_run_id() : "-",
        .snap_origin = s_operation.origin,
    };
    lab02_trace_emit_payload("snap_start", 0, s_operation.focus,
                             s_operation.message_id, &payload);
    if (start == target_x) {
        finish(id, LAB02_SNAP_SUCCESS, "no_motion");
        return;
    }
    lv_anim_t a;
    lv_anim_init(&a);
    lv_anim_set_var(&a, st);
    lv_anim_set_user_data(&a, (void *)(uintptr_t)id);
    lv_anim_set_values(&a, start, target_x);
    lv_anim_set_duration(&a, 220);
    lv_anim_set_custom_exec_cb(&a, scroll_x_exec);
    lv_anim_set_completed_cb(&a, snap_anim_done);
    lv_anim_set_deleted_cb(&a, snap_anim_deleted);
    if (!lv_anim_start(&a))
        finish(id, LAB02_SNAP_CANCELLED, "animation_allocation_failed");
}

uint32_t lab02_snap_current_id(void)
{
    return atomic_load(&s_snap_id);
}
