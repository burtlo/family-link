#include "fl_shell_model.h"

#include <stdio.h>
#include <string.h>

static const char *panel_text(fl_shell_stage_t stage, fl_shell_slot_t slot)
{
    static const char *const upper[FL_SHELL_STAGE_COUNT] = {
        "Roster", "PIN", "Hangout", "Inbox", "Volume", "Record", "Receipt", "Picker"};
    static const char *const lower[FL_SHELL_STAGE_COUNT] = {
        "Choose a user", "Enter PIN", "No messages", "Synthetic inbox",
        "Desk volume slider", "Synthetic record", "Synthetic receipt", "Choose people"};
    if ((unsigned)stage >= FL_SHELL_STAGE_COUNT)
        return "Invalid";
    return slot == FL_SHELL_SLOT_UPPER ? upper[stage] : lower[stage];
}

static void mount_panels(fl_shell_session_t *session, fl_shell_stage_t stage)
{
    for (unsigned slot = 0; slot < 2; ++slot) {
        fl_shell_panel_t *panel = &session->panels[slot];
        panel->generation = ++session->next_generation;
        panel->stage = stage;
        panel->slot = (fl_shell_slot_t)slot;
        panel->focused_synthetic_id = 100u + (uint32_t)slot;
        panel->alive = true;
        panel->updates = 0;
        (void)snprintf(panel->text, sizeof(panel->text), "%s", panel_text(stage, panel->slot));
    }
}

static void terminal(fl_shell_session_t *session, uint32_t operation_id,
                     fl_shell_terminal_t outcome, const char *reason)
{
    for (size_t i = 0; i < session->terminal_ids_count; ++i) {
        if (session->terminal_ids[i] == operation_id)
            return;
    }
    if (session->terminal_ids_count < FL_SHELL_TERMINAL_HISTORY) {
        session->terminal_ids[session->terminal_ids_count++] = operation_id;
    } else {
        session->terminal_ids[session->terminal_ids_next] = operation_id;
        session->terminal_ids_next = (session->terminal_ids_next + 1) % FL_SHELL_TERMINAL_HISTORY;
    }
    session->terminal_total++;
    session->last_result.operation_id = operation_id;
    session->last_result.terminal = outcome;
    session->last_result.reason = reason;
    session->last_result.terminal_count = 1;
}

static bool operation_is_terminal(const fl_shell_session_t *session, uint32_t operation_id)
{
    for (size_t i = 0; i < session->terminal_ids_count; ++i) {
        if (session->terminal_ids[i] == operation_id)
            return true;
    }
    return false;
}

/* Operation ids increase for each newly submitted operation in one session.
 * A fixed watermark makes reuse detection constant-space; the bounded terminal
 * ring remains diagnostic history only. */
static bool admit_operation(fl_shell_session_t *session, uint32_t operation_id)
{
    if (operation_id <= session->operation_id_watermark)
        return false;
    session->operation_id_watermark = operation_id;
    return true;
}

fl_shell_slot_geometry_t fl_shell_geometry(fl_shell_slot_t slot)
{
    fl_shell_slot_geometry_t geometry;
    if (slot == FL_SHELL_SLOT_UPPER) {
        geometry.chrome = (fl_shell_rect_t){0, 0, 320, 80};
        geometry.content = (fl_shell_rect_t){12, 12, 296, 56};
    } else {
        geometry.chrome = (fl_shell_rect_t){0, 80, 320, 160};
        geometry.content = (fl_shell_rect_t){12, 92, 296, 136};
    }
    return geometry;
}

void fl_shell_session_init(fl_shell_session_t *session, uint32_t owner_id, fl_shell_stage_t initial_stage)
{
    memset(session, 0, sizeof(*session));
    session->owner_id = owner_id;
    session->stage = initial_stage;
    session->cancel_stage_is_source = true;
    /* One shell-owned transition timer is reused for every stage cycle. */
    session->timer_count = 1;
    mount_panels(session, initial_stage);
}

bool fl_shell_begin_stage_transition(fl_shell_session_t *session, uint32_t writer_id,
                                     fl_shell_stage_t destination, uint32_t operation_id)
{
    if (!admit_operation(session, operation_id))
        return false;
    if (writer_id != session->owner_id) {
        terminal(session, operation_id, FL_SHELL_TERMINAL_FAILURE, "second_writer");
        return false;
    }
    if ((unsigned)destination >= FL_SHELL_STAGE_COUNT) {
        terminal(session, operation_id, FL_SHELL_TERMINAL_FAILURE, "invalid_stage");
        return false;
    }
    if (session->transitioning) {
        terminal(session, operation_id, FL_SHELL_TERMINAL_FAILURE, "transition_busy");
        return false;
    }
    if (destination == session->stage) {
        terminal(session, operation_id, FL_SHELL_TERMINAL_SUCCESS, "idempotent");
        return true;
    }
    session->transitioning = true;
    session->transition_source = session->stage;
    session->transition_destination = destination;
    session->transition_operation_id = operation_id;
    session->last_result.operation_id = operation_id;
    /* Invalidate before deleting/replacing panel-owned handles. */
    session->panels[0].alive = false;
    session->panels[1].alive = false;
    return true;
}

bool fl_shell_finish_stage_transition(fl_shell_session_t *session, uint32_t operation_id)
{
    if (!session->transitioning || session->transition_operation_id != operation_id) {
        if (!admit_operation(session, operation_id))
            return false;
        session->stale_callbacks++;
        terminal(session, operation_id, FL_SHELL_TERMINAL_STALE, "stale_transition");
        return false;
    }
    session->stage = session->transition_destination;
    session->transitioning = false;
    mount_panels(session, session->stage);
    terminal(session, operation_id, FL_SHELL_TERMINAL_SUCCESS, "stage_changed");
    return true;
}

bool fl_shell_cancel_stage_transition(fl_shell_session_t *session, uint32_t operation_id)
{
    if (!session->transitioning || session->transition_operation_id != operation_id) {
        if (!admit_operation(session, operation_id))
            return false;
        session->stale_callbacks++;
        terminal(session, operation_id, FL_SHELL_TERMINAL_STALE, "stale_cancel");
        return false;
    }
    session->stage = session->transition_source;
    session->transitioning = false;
    mount_panels(session, session->stage);
    /* Timer is shell-owned, so cancellation neither creates nor destroys it. */
    terminal(session, operation_id, FL_SHELL_TERMINAL_CANCELLED, "cancelled");
    return true;
}

bool fl_shell_apply_panel_data(fl_shell_session_t *session, fl_shell_slot_t slot,
                               uint32_t generation, uint32_t operation_id, const char *text,
                               fl_shell_update_level_t update_level)
{
    if (operation_id == 0 || (unsigned)slot > FL_SHELL_SLOT_LOWER || !text ||
        (update_level != FL_SHELL_UPDATE_PROPERTY && update_level != FL_SHELL_UPDATE_SUBTREE &&
         update_level != FL_SHELL_UPDATE_SCENE)) {
        return false;
    }
    if (!admit_operation(session, operation_id) || operation_is_terminal(session, operation_id))
        return false;
    if (update_level == FL_SHELL_UPDATE_SCENE) {
        terminal(session, operation_id, FL_SHELL_TERMINAL_FAILURE, "level2_on_data");
        return false;
    }
    fl_shell_panel_t *panel = &session->panels[slot];
    if (!panel->alive || panel->generation != generation || session->transitioning) {
        session->stale_callbacks++;
        terminal(session, operation_id, FL_SHELL_TERMINAL_STALE, "stale_panel");
        return false;
    }
    session->handle_uses++;
    (void)snprintf(panel->text, sizeof(panel->text), "%s", text);
    panel->updates++;
    terminal(session, operation_id, FL_SHELL_TERMINAL_SUCCESS, "panel_data_applied");
    return true;
}

bool fl_shell_panel_handle(const fl_shell_session_t *session, fl_shell_slot_t slot,
                           uint32_t *generation_out)
{
    if ((unsigned)slot > FL_SHELL_SLOT_LOWER || !generation_out || !session->panels[slot].alive)
        return false;
    *generation_out = session->panels[slot].generation;
    return true;
}

bool fl_shell_event_queue_push(fl_shell_session_t *session, uint32_t operation_id)
{
    if (!admit_operation(session, operation_id))
        return false;
    if (session->event_count == FL_SHELL_EVENT_CAPACITY) {
        session->queue_overflows++;
        terminal(session, operation_id, FL_SHELL_TERMINAL_FAILURE, "overflow");
        return false;
    }
    size_t tail = (session->event_head + session->event_count) % FL_SHELL_EVENT_CAPACITY;
    session->event_ids[tail] = operation_id;
    session->event_count++;
    return true;
}

bool fl_shell_event_queue_pop(fl_shell_session_t *session, uint32_t *operation_id_out)
{
    if (!session->event_count || !operation_id_out)
        return false;
    *operation_id_out = session->event_ids[session->event_head];
    session->event_head = (session->event_head + 1) % FL_SHELL_EVENT_CAPACITY;
    session->event_count--;
    terminal(session, *operation_id_out, FL_SHELL_TERMINAL_SUCCESS, "queue_dequeued");
    return true;
}
