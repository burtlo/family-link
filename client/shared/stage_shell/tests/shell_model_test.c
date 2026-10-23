#include "fl_shell_model.h"
#include "fl_shell_cancel_schedule.h"

#include <assert.h>
#include <string.h>

static void test_fl_shell_single_writer(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 17, FL_SHELL_STAGE_ROSTER);
    assert(fl_shell_begin_stage_transition(&session, 17, FL_SHELL_STAGE_PIN, 1));
    assert(fl_shell_finish_stage_transition(&session, 1));
    uint32_t generation;
    assert(fl_shell_panel_handle(&session, FL_SHELL_SLOT_LOWER, &generation));
    assert(fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_LOWER, generation, 2,
                                  "12", FL_SHELL_UPDATE_PROPERTY));
    uint32_t terminal_total = session.terminal_total;
    assert(!fl_shell_begin_stage_transition(&session, 18, FL_SHELL_STAGE_ROSTER, 3));
    assert(session.stage == FL_SHELL_STAGE_PIN && !session.transitioning);
    assert(strcmp(session.panels[FL_SHELL_SLOT_LOWER].text, "12") == 0);
    assert(session.terminal_total == terminal_total + 1);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_FAILURE);
    assert(strcmp(session.last_result.reason, "second_writer") == 0);
    assert(session.last_result.operation_id == 3 && session.last_result.terminal_count == 1);
}

static void test_fl_shell_terminal_operation_id_reuse(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_PIN, 44));
    assert(fl_shell_finish_stage_transition(&session, 44));
    assert(session.terminal_total == 1);
    assert(session.terminal_ids_count == 1 && session.terminal_ids[0] == 44);
    assert(session.last_result.operation_id == 44);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);

    fl_shell_panel_t panels_before[2];
    memcpy(panels_before, session.panels, sizeof(panels_before));
    uint32_t timers = session.timer_count;
    assert(!fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_ROSTER, 44));
    assert(session.stage == FL_SHELL_STAGE_PIN && !session.transitioning);
    assert(memcmp(session.panels, panels_before, sizeof(panels_before)) == 0);
    assert(session.timer_count == timers);
    assert(session.terminal_total == 1);
    assert(session.terminal_ids_count == 1 && session.terminal_ids[0] == 44);
    assert(session.last_result.operation_id == 44);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
}

static void test_fl_shell_geometry(void)
{
    fl_shell_slot_geometry_t upper = fl_shell_geometry(FL_SHELL_SLOT_UPPER);
    fl_shell_slot_geometry_t lower = fl_shell_geometry(FL_SHELL_SLOT_LOWER);
    assert(upper.chrome.x == 0 && upper.chrome.y == 0);
    assert(upper.chrome.width == 320 && upper.chrome.height == 80);
    assert(lower.chrome.x == 0 && lower.chrome.y == 80);
    assert(lower.chrome.width == 320 && lower.chrome.height == 160);
    assert(upper.content.x == 12 && upper.content.y == 12);
    assert(upper.content.width == 296 && upper.content.height == 56);
    assert(lower.content.x == 12 && lower.content.y == 92);
    assert(lower.content.width == 296 && lower.content.height == 136);
}

static void test_fl_shell_idempotent_enter(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t first = session.panels[0].generation;
    uint32_t second = session.panels[1].generation;
    uint32_t timers = session.timer_count;
    assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_ROSTER, 20));
    assert(!session.transitioning && session.timer_count == timers);
    assert(session.panels[0].generation == first && session.panels[1].generation == second);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
}

static void test_fl_shell_data_during_exit(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t generation;
    assert(fl_shell_panel_handle(&session, FL_SHELL_SLOT_UPPER, &generation));
    uint32_t uses = session.handle_uses;
    assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_PIN, 21));
    uint32_t terminals = session.terminal_total;
    assert(!fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_UPPER, generation, 22,
                                   "late badge", FL_SHELL_UPDATE_PROPERTY));
    assert(session.handle_uses == uses && session.panels[0].updates == 0);
    assert(session.terminal_total == terminals + 1);
    assert(session.last_result.operation_id == 22);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_STALE);
    assert(session.last_result.terminal_count == 1);
    assert(fl_shell_finish_stage_transition(&session, 21));
    assert(strcmp(session.panels[0].text, "PIN") == 0);
    assert(session.last_result.operation_id == 21);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
    assert(session.terminal_total == terminals + 2);
    assert(!fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_UPPER,
                                   session.panels[0].generation, 22,
                                   "duplicate callback", FL_SHELL_UPDATE_PROPERTY));
    assert(session.terminal_total == terminals + 2);
    assert(strcmp(session.panels[0].text, "PIN") == 0);
}

static void test_fl_shell_stale_panel(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t generation;
    assert(fl_shell_panel_handle(&session, FL_SHELL_SLOT_LOWER, &generation));
    assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_INBOX, 22));
    assert(fl_shell_finish_stage_transition(&session, 22));
    assert(!fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_LOWER, generation, 24,
                                   "old panel", FL_SHELL_UPDATE_SUBTREE));
    assert(session.handle_uses == 0);
    assert(session.stale_callbacks == 1);
    assert(session.last_result.operation_id == 24);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_STALE);
    assert(session.last_result.terminal_count == 1);
    assert(session.panels[1].alive);
    assert(strcmp(session.panels[1].text, "Synthetic inbox") == 0);
}

static void test_fl_shell_cancel(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t timers = session.timer_count;
    assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_PIN, 23));
    assert(fl_shell_cancel_stage_transition(&session, 23));
    assert(session.stage == FL_SHELL_STAGE_ROSTER);
    assert(!session.transitioning && session.timer_count == timers);
    assert(session.last_result.operation_id == 23);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_CANCELLED);
    assert(session.last_result.terminal_count == 1);
    uint32_t terminals = session.terminal_total;
    assert(!fl_shell_cancel_stage_transition(&session, 23));
    assert(session.terminal_total == terminals);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_CANCELLED);
}

static void test_fl_shell_cancel_schedule(void)
{
    /* A delayed UI-timer callback after the nominal duration still chooses
     * cancellation first while the scripted probe owns the active transition. */
    assert(fl_shell_animation_action(true, 199999, 200000, 400000) ==
           FL_SHELL_ANIMATION_ADVANCE);
    assert(fl_shell_animation_action(true, 200000, 200000, 400000) ==
           FL_SHELL_ANIMATION_CANCEL);
    assert(fl_shell_animation_action(true, 420000, 200000, 400000) ==
           FL_SHELL_ANIMATION_CANCEL);
    assert(fl_shell_animation_action(false, 420000, 200000, 400000) ==
           FL_SHELL_ANIMATION_FINISH);

    fl_shell_cancel_control_t control = {0};
    assert(fl_shell_cancel_arm(&control, false, false));
    assert(control.armed && !control.transition_scheduled);
    assert(!fl_shell_cancel_arm(&control, false, true));
    assert(fl_shell_cancel_transition_started(&control, false));
    assert(!control.armed && control.transition_scheduled);
    assert(fl_shell_animation_action(control.transition_scheduled, 199999,
                                  200000, 400000) == FL_SHELL_ANIMATION_ADVANCE);
    assert(fl_shell_animation_action(control.transition_scheduled, 200000,
                                  200000, 400000) == FL_SHELL_ANIMATION_CANCEL);
    fl_shell_cancel_transition_finished(&control);
    assert(!control.armed && !control.transition_scheduled);

    /* A rejected model transition clears the arm; a later accepted transition
     * must not inherit it. */
    assert(fl_shell_cancel_arm(&control, false, false));
    fl_shell_session_t rejected_session;
    fl_shell_session_init(&rejected_session, 1, FL_SHELL_STAGE_ROSTER);
    assert(!fl_shell_begin_stage_transition(&rejected_session, 2,
                                         FL_SHELL_STAGE_PIN, 1));
    assert(fl_shell_cancel_control_clear(&control, false));
    assert(!control.armed && !control.transition_scheduled);
    assert(!fl_shell_cancel_transition_started(&control, false));
    fl_shell_cancel_transition_finished(&control);

    /* Mount recovery follows the same no-motion rejection contract. */
    assert(fl_shell_cancel_arm(&control, false, false));
    assert(fl_shell_cancel_control_clear(&control, false));
    assert(!fl_shell_cancel_transition_started(&control, false));

    /* A direct cancel consumes the scheduled arm; finishing it cannot leave
     * a second cancellation queued for the following transition. */
    assert(fl_shell_cancel_arm(&control, false, false));
    assert(fl_shell_cancel_transition_started(&control, false));
    assert(fl_shell_cancel_control_clear(&control, false));
    assert(!control.armed && !control.transition_scheduled);
    assert(!fl_shell_cancel_transition_started(&control, false));

    /* The scripted harness does not arm or consume the manual control. */
    control.armed = true;
    control.transition_scheduled = true;
    assert(!fl_shell_cancel_control_clear(&control, true));
    assert(control.armed && control.transition_scheduled);
    control.armed = false;
    control.transition_scheduled = false;
    assert(!fl_shell_cancel_arm(&control, true, false));
    assert(!fl_shell_cancel_transition_started(&control, true));
    assert(!control.armed && !control.transition_scheduled);
}

static void test_fl_shell_level2_control(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t generation;
    assert(fl_shell_panel_handle(&session, FL_SHELL_SLOT_UPPER, &generation));
    assert(!fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_UPPER, generation, 25,
                                   "forced rebuild", FL_SHELL_UPDATE_SCENE));
    assert(session.panels[0].updates == 0 && session.handle_uses == 0);
    assert(session.terminal_total == 1);
    assert(session.last_result.operation_id == 25);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_FAILURE);
    assert(strcmp(session.last_result.reason, "level2_on_data") == 0);
    assert(session.last_result.terminal_count == 1);
    assert(session.terminal_ids_count == 1 && session.terminal_ids[0] == 25);

    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    assert(!fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_UPPER, generation, 25,
                                   NULL, FL_SHELL_UPDATE_SCENE));
    assert(!fl_shell_apply_panel_data(&session, (fl_shell_slot_t)2, generation, 26,
                                   "malformed", FL_SHELL_UPDATE_SCENE));
    assert(!fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_UPPER, generation, 27,
                                   "malformed", (fl_shell_update_level_t)99));
    assert(session.terminal_total == 0);
    assert(session.terminal_ids_count == 0);
    assert(session.operation_id_watermark == 0);
    assert(session.panels[0].updates == 0 && session.handle_uses == 0);
}

static void test_fl_shell_pending_capacity(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    for (uint32_t operation_id = 1; operation_id <= FL_SHELL_EVENT_CAPACITY; ++operation_id)
        assert(fl_shell_event_queue_push(&session, operation_id));
    assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_PIN,
                                        FL_SHELL_EVENT_CAPACITY + 1u));
    size_t pending_operations = session.event_count + (session.transitioning ? 1u : 0u);
    assert(session.event_count == FL_SHELL_EVENT_CAPACITY);
    assert(session.transitioning);
    assert(pending_operations == FL_SHELL_MAX_CONCURRENT_OPERATIONS);
    assert(pending_operations == 9u);
    assert(session.terminal_total == 0);
}

static void test_fl_shell_data_levels(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t generation;
    assert(fl_shell_panel_handle(&session, FL_SHELL_SLOT_LOWER, &generation));
    uint32_t focused_id = session.panels[FL_SHELL_SLOT_LOWER].focused_synthetic_id;
    assert(fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_LOWER, generation, 26,
                                  "badge updated", FL_SHELL_UPDATE_PROPERTY));
    assert(fl_shell_apply_panel_data(&session, FL_SHELL_SLOT_LOWER, generation, 27,
                                  "roster revised", FL_SHELL_UPDATE_SUBTREE));
    assert(session.panels[FL_SHELL_SLOT_LOWER].focused_synthetic_id == focused_id);
    assert(session.panels[FL_SHELL_SLOT_LOWER].updates == 2);
    assert(session.panels[FL_SHELL_SLOT_UPPER].updates == 0);
    assert(session.terminal_total == 2);
    assert(session.terminal_ids_count == 2);
    assert(session.terminal_ids[0] == 26 && session.terminal_ids[1] == 27);
    assert(session.last_result.operation_id == 27);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
}

static void test_fl_shell_cycle(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t timers = session.timer_count;
    for (uint32_t cycle = 0; cycle < 20; ++cycle) {
        for (unsigned stage = 1; stage < FL_SHELL_STAGE_COUNT; ++stage) {
            uint32_t id = cycle * 16u + stage;
            assert(fl_shell_begin_stage_transition(&session, 1, (fl_shell_stage_t)stage, id));
            assert(fl_shell_finish_stage_transition(&session, id));
            assert(session.last_result.terminal_count == 1);
        }
        uint32_t id = cycle * 16u + 15u;
        assert(fl_shell_begin_stage_transition(&session, 1, FL_SHELL_STAGE_ROSTER, id));
        assert(fl_shell_finish_stage_transition(&session, id));
    }
    assert(session.timer_count == timers);
}

static void test_fl_shell_event_overflow(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    for (uint32_t id = 1; id <= FL_SHELL_EVENT_CAPACITY; ++id)
        assert(fl_shell_event_queue_push(&session, id));
    assert(!fl_shell_event_queue_push(&session, 99));
    assert(session.event_count == FL_SHELL_EVENT_CAPACITY);
    assert(session.queue_overflows == 1);
    assert(session.terminal_total == 1);
    assert(session.last_result.operation_id == 99);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_FAILURE);
    assert(strcmp(session.last_result.reason, "overflow") == 0);
    assert(session.last_result.terminal_count == 1);
    uint32_t id;
    for (uint32_t expected = 1; expected <= FL_SHELL_EVENT_CAPACITY; ++expected) {
        assert(fl_shell_event_queue_pop(&session, &id) && id == expected);
        assert(session.terminal_total == expected + 1);
        assert(session.last_result.operation_id == expected);
        assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
        assert(strcmp(session.last_result.reason, "queue_dequeued") == 0);
        assert(session.last_result.terminal_count == 1);
    }
    assert(!fl_shell_event_queue_pop(&session, &id));
    assert(session.terminal_total == FL_SHELL_EVENT_CAPACITY + 1);
    assert(session.terminal_ids_count == FL_SHELL_EVENT_CAPACITY + 1);
}

static void test_fl_shell_event_queue_terminals(void)
{
    fl_shell_session_t session;
    fl_shell_session_init(&session, 1, FL_SHELL_STAGE_ROSTER);
    uint32_t id = 0;

    assert(fl_shell_event_queue_push(&session, 10));
    assert(session.event_count == 1 && session.terminal_total == 0);
    assert(!fl_shell_event_queue_pop(&session, NULL));
    assert(session.event_count == 1 && session.terminal_total == 0);
    assert(fl_shell_event_queue_pop(&session, &id) && id == 10);
    assert(session.event_count == 0 && session.terminal_total == 1);
    assert(session.last_result.operation_id == 10);
    assert(session.last_result.terminal == FL_SHELL_TERMINAL_SUCCESS);
    assert(strcmp(session.last_result.reason, "queue_dequeued") == 0);
    assert(session.last_result.terminal_count == 1);

    assert(!fl_shell_event_queue_push(&session, 10));
    assert(!fl_shell_event_queue_push(&session, 9));
    assert(session.event_count == 0 && session.terminal_total == 1);
    assert(session.operation_id_watermark == 10);
    assert(session.queue_overflows == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "single_writer") == 0) test_fl_shell_single_writer();
    else if (strcmp(argv[1], "terminal_id_reuse") == 0) test_fl_shell_terminal_operation_id_reuse();
    else if (strcmp(argv[1], "geometry") == 0) test_fl_shell_geometry();
    else if (strcmp(argv[1], "idempotent_enter") == 0) test_fl_shell_idempotent_enter();
    else if (strcmp(argv[1], "data_during_exit") == 0) test_fl_shell_data_during_exit();
    else if (strcmp(argv[1], "stale_panel") == 0) test_fl_shell_stale_panel();
    else if (strcmp(argv[1], "cancel") == 0) test_fl_shell_cancel();
    else if (strcmp(argv[1], "cancel_schedule") == 0) test_fl_shell_cancel_schedule();
    else if (strcmp(argv[1], "level2_control") == 0) test_fl_shell_level2_control();
    else if (strcmp(argv[1], "pending_capacity") == 0) test_fl_shell_pending_capacity();
    else if (strcmp(argv[1], "data_levels") == 0) test_fl_shell_data_levels();
    else if (strcmp(argv[1], "cycle") == 0) test_fl_shell_cycle();
    else if (strcmp(argv[1], "overflow") == 0) test_fl_shell_event_overflow();
    else if (strcmp(argv[1], "queue_terminals") == 0) test_fl_shell_event_queue_terminals();
    else return 2;
    return 0;
}
