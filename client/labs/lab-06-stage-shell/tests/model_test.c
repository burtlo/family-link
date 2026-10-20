#include "lab06_model.h"
#include "lab06_cancel_schedule.h"

#include <assert.h>
#include <string.h>

static void test_lab06_single_writer(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 17, LAB06_STAGE_ROSTER);
    assert(lab06_begin_stage_transition(&session, 17, LAB06_STAGE_PIN, 1));
    assert(lab06_finish_stage_transition(&session, 1));
    uint32_t generation;
    assert(lab06_panel_handle(&session, LAB06_SLOT_LOWER, &generation));
    assert(lab06_apply_panel_data(&session, LAB06_SLOT_LOWER, generation, 2,
                                  "12", LAB06_UPDATE_PROPERTY));
    uint32_t terminal_total = session.terminal_total;
    assert(!lab06_begin_stage_transition(&session, 18, LAB06_STAGE_ROSTER, 3));
    assert(session.stage == LAB06_STAGE_PIN && !session.transitioning);
    assert(strcmp(session.panels[LAB06_SLOT_LOWER].text, "12") == 0);
    assert(session.terminal_total == terminal_total + 1);
    assert(session.last_result.terminal == LAB06_TERMINAL_FAILURE);
    assert(strcmp(session.last_result.reason, "second_writer") == 0);
    assert(session.last_result.operation_id == 3 && session.last_result.terminal_count == 1);
}

static void test_lab06_terminal_operation_id_reuse(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_PIN, 44));
    assert(lab06_finish_stage_transition(&session, 44));
    assert(session.terminal_total == 1);
    assert(session.terminal_ids_count == 1 && session.terminal_ids[0] == 44);
    assert(session.last_result.operation_id == 44);
    assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);

    lab06_panel_t panels_before[2];
    memcpy(panels_before, session.panels, sizeof(panels_before));
    uint32_t timers = session.timer_count;
    assert(!lab06_begin_stage_transition(&session, 1, LAB06_STAGE_ROSTER, 44));
    assert(session.stage == LAB06_STAGE_PIN && !session.transitioning);
    assert(memcmp(session.panels, panels_before, sizeof(panels_before)) == 0);
    assert(session.timer_count == timers);
    assert(session.terminal_total == 1);
    assert(session.terminal_ids_count == 1 && session.terminal_ids[0] == 44);
    assert(session.last_result.operation_id == 44);
    assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
}

static void test_lab06_geometry(void)
{
    lab06_slot_geometry_t upper = lab06_geometry(LAB06_SLOT_UPPER);
    lab06_slot_geometry_t lower = lab06_geometry(LAB06_SLOT_LOWER);
    assert(upper.chrome.x == 0 && upper.chrome.y == 0);
    assert(upper.chrome.width == 320 && upper.chrome.height == 80);
    assert(lower.chrome.x == 0 && lower.chrome.y == 80);
    assert(lower.chrome.width == 320 && lower.chrome.height == 160);
    assert(upper.content.x == 12 && upper.content.y == 12);
    assert(upper.content.width == 296 && upper.content.height == 56);
    assert(lower.content.x == 12 && lower.content.y == 92);
    assert(lower.content.width == 296 && lower.content.height == 136);
}

static void test_lab06_idempotent_enter(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t first = session.panels[0].generation;
    uint32_t second = session.panels[1].generation;
    uint32_t timers = session.timer_count;
    assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_ROSTER, 20));
    assert(!session.transitioning && session.timer_count == timers);
    assert(session.panels[0].generation == first && session.panels[1].generation == second);
    assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
}

static void test_lab06_data_during_exit(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t generation;
    assert(lab06_panel_handle(&session, LAB06_SLOT_UPPER, &generation));
    uint32_t uses = session.handle_uses;
    assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_PIN, 21));
    uint32_t terminals = session.terminal_total;
    assert(!lab06_apply_panel_data(&session, LAB06_SLOT_UPPER, generation, 22,
                                   "late badge", LAB06_UPDATE_PROPERTY));
    assert(session.handle_uses == uses && session.panels[0].updates == 0);
    assert(session.terminal_total == terminals + 1);
    assert(session.last_result.operation_id == 22);
    assert(session.last_result.terminal == LAB06_TERMINAL_STALE);
    assert(session.last_result.terminal_count == 1);
    assert(lab06_finish_stage_transition(&session, 21));
    assert(strcmp(session.panels[0].text, "PIN") == 0);
    assert(session.last_result.operation_id == 21);
    assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
    assert(session.terminal_total == terminals + 2);
    assert(!lab06_apply_panel_data(&session, LAB06_SLOT_UPPER,
                                   session.panels[0].generation, 22,
                                   "duplicate callback", LAB06_UPDATE_PROPERTY));
    assert(session.terminal_total == terminals + 2);
    assert(strcmp(session.panels[0].text, "PIN") == 0);
}

static void test_lab06_stale_panel(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t generation;
    assert(lab06_panel_handle(&session, LAB06_SLOT_LOWER, &generation));
    assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_INBOX, 22));
    assert(lab06_finish_stage_transition(&session, 22));
    assert(!lab06_apply_panel_data(&session, LAB06_SLOT_LOWER, generation, 24,
                                   "old panel", LAB06_UPDATE_SUBTREE));
    assert(session.handle_uses == 0);
    assert(session.stale_callbacks == 1);
    assert(session.last_result.operation_id == 24);
    assert(session.last_result.terminal == LAB06_TERMINAL_STALE);
    assert(session.last_result.terminal_count == 1);
    assert(session.panels[1].alive);
    assert(strcmp(session.panels[1].text, "Synthetic inbox") == 0);
}

static void test_lab06_cancel(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t timers = session.timer_count;
    assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_PIN, 23));
    assert(lab06_cancel_stage_transition(&session, 23));
    assert(session.stage == LAB06_STAGE_ROSTER);
    assert(!session.transitioning && session.timer_count == timers);
    assert(session.last_result.operation_id == 23);
    assert(session.last_result.terminal == LAB06_TERMINAL_CANCELLED);
    assert(session.last_result.terminal_count == 1);
    uint32_t terminals = session.terminal_total;
    assert(!lab06_cancel_stage_transition(&session, 23));
    assert(session.terminal_total == terminals);
    assert(session.last_result.terminal == LAB06_TERMINAL_CANCELLED);
}

static void test_lab06_cancel_schedule(void)
{
    /* A delayed UI-timer callback after the nominal duration still chooses
     * cancellation first while the scripted probe owns the active transition. */
    assert(lab06_animation_action(true, 199999, 200000, 400000) ==
           LAB06_ANIMATION_ADVANCE);
    assert(lab06_animation_action(true, 200000, 200000, 400000) ==
           LAB06_ANIMATION_CANCEL);
    assert(lab06_animation_action(true, 420000, 200000, 400000) ==
           LAB06_ANIMATION_CANCEL);
    assert(lab06_animation_action(false, 420000, 200000, 400000) ==
           LAB06_ANIMATION_FINISH);

    lab06_cancel_control_t control = {0};
    assert(lab06_cancel_arm(&control, false, false));
    assert(control.armed && !control.transition_scheduled);
    assert(!lab06_cancel_arm(&control, false, true));
    assert(lab06_cancel_transition_started(&control, false));
    assert(!control.armed && control.transition_scheduled);
    assert(lab06_animation_action(control.transition_scheduled, 199999,
                                  200000, 400000) == LAB06_ANIMATION_ADVANCE);
    assert(lab06_animation_action(control.transition_scheduled, 200000,
                                  200000, 400000) == LAB06_ANIMATION_CANCEL);
    lab06_cancel_transition_finished(&control);
    assert(!control.armed && !control.transition_scheduled);

    /* A rejected model transition clears the arm; a later accepted transition
     * must not inherit it. */
    assert(lab06_cancel_arm(&control, false, false));
    lab06_session_t rejected_session;
    lab06_session_init(&rejected_session, 1, LAB06_STAGE_ROSTER);
    assert(!lab06_begin_stage_transition(&rejected_session, 2,
                                         LAB06_STAGE_PIN, 1));
    assert(lab06_cancel_control_clear(&control, false));
    assert(!control.armed && !control.transition_scheduled);
    assert(!lab06_cancel_transition_started(&control, false));
    lab06_cancel_transition_finished(&control);

    /* Mount recovery follows the same no-motion rejection contract. */
    assert(lab06_cancel_arm(&control, false, false));
    assert(lab06_cancel_control_clear(&control, false));
    assert(!lab06_cancel_transition_started(&control, false));

    /* A direct cancel consumes the scheduled arm; finishing it cannot leave
     * a second cancellation queued for the following transition. */
    assert(lab06_cancel_arm(&control, false, false));
    assert(lab06_cancel_transition_started(&control, false));
    assert(lab06_cancel_control_clear(&control, false));
    assert(!control.armed && !control.transition_scheduled);
    assert(!lab06_cancel_transition_started(&control, false));

    /* The scripted harness does not arm or consume the manual control. */
    control.armed = true;
    control.transition_scheduled = true;
    assert(!lab06_cancel_control_clear(&control, true));
    assert(control.armed && control.transition_scheduled);
    control.armed = false;
    control.transition_scheduled = false;
    assert(!lab06_cancel_arm(&control, true, false));
    assert(!lab06_cancel_transition_started(&control, true));
    assert(!control.armed && !control.transition_scheduled);
}

static void test_lab06_level2_control(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t generation;
    assert(lab06_panel_handle(&session, LAB06_SLOT_UPPER, &generation));
    assert(!lab06_apply_panel_data(&session, LAB06_SLOT_UPPER, generation, 25,
                                   "forced rebuild", LAB06_UPDATE_SCENE));
    assert(session.panels[0].updates == 0 && session.handle_uses == 0);
    assert(session.terminal_total == 1);
    assert(session.last_result.operation_id == 25);
    assert(session.last_result.terminal == LAB06_TERMINAL_FAILURE);
    assert(strcmp(session.last_result.reason, "level2_on_data") == 0);
    assert(session.last_result.terminal_count == 1);
    assert(session.terminal_ids_count == 1 && session.terminal_ids[0] == 25);

    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    assert(!lab06_apply_panel_data(&session, LAB06_SLOT_UPPER, generation, 25,
                                   NULL, LAB06_UPDATE_SCENE));
    assert(!lab06_apply_panel_data(&session, (lab06_slot_t)2, generation, 26,
                                   "malformed", LAB06_UPDATE_SCENE));
    assert(!lab06_apply_panel_data(&session, LAB06_SLOT_UPPER, generation, 27,
                                   "malformed", (lab06_update_level_t)99));
    assert(session.terminal_total == 0);
    assert(session.terminal_ids_count == 0);
    assert(session.operation_id_watermark == 0);
    assert(session.panels[0].updates == 0 && session.handle_uses == 0);
}

static void test_lab06_pending_capacity(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    for (uint32_t operation_id = 1; operation_id <= LAB06_EVENT_CAPACITY; ++operation_id)
        assert(lab06_event_queue_push(&session, operation_id));
    assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_PIN,
                                        LAB06_EVENT_CAPACITY + 1u));
    size_t pending_operations = session.event_count + (session.transitioning ? 1u : 0u);
    assert(session.event_count == LAB06_EVENT_CAPACITY);
    assert(session.transitioning);
    assert(pending_operations == LAB06_MAX_CONCURRENT_OPERATIONS);
    assert(pending_operations == 9u);
    assert(session.terminal_total == 0);
}

static void test_lab06_data_levels(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t generation;
    assert(lab06_panel_handle(&session, LAB06_SLOT_LOWER, &generation));
    uint32_t focused_id = session.panels[LAB06_SLOT_LOWER].focused_synthetic_id;
    assert(lab06_apply_panel_data(&session, LAB06_SLOT_LOWER, generation, 26,
                                  "badge updated", LAB06_UPDATE_PROPERTY));
    assert(lab06_apply_panel_data(&session, LAB06_SLOT_LOWER, generation, 27,
                                  "roster revised", LAB06_UPDATE_SUBTREE));
    assert(session.panels[LAB06_SLOT_LOWER].focused_synthetic_id == focused_id);
    assert(session.panels[LAB06_SLOT_LOWER].updates == 2);
    assert(session.panels[LAB06_SLOT_UPPER].updates == 0);
    assert(session.terminal_total == 2);
    assert(session.terminal_ids_count == 2);
    assert(session.terminal_ids[0] == 26 && session.terminal_ids[1] == 27);
    assert(session.last_result.operation_id == 27);
    assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
    assert(session.last_result.terminal_count == 1);
}

static void test_lab06_cycle(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t timers = session.timer_count;
    for (uint32_t cycle = 0; cycle < 20; ++cycle) {
        for (unsigned stage = 1; stage < LAB06_STAGE_COUNT; ++stage) {
            uint32_t id = cycle * 16u + stage;
            assert(lab06_begin_stage_transition(&session, 1, (lab06_stage_t)stage, id));
            assert(lab06_finish_stage_transition(&session, id));
            assert(session.last_result.terminal_count == 1);
        }
        uint32_t id = cycle * 16u + 15u;
        assert(lab06_begin_stage_transition(&session, 1, LAB06_STAGE_ROSTER, id));
        assert(lab06_finish_stage_transition(&session, id));
    }
    assert(session.timer_count == timers);
}

static void test_lab06_event_overflow(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    for (uint32_t id = 1; id <= LAB06_EVENT_CAPACITY; ++id)
        assert(lab06_event_queue_push(&session, id));
    assert(!lab06_event_queue_push(&session, 99));
    assert(session.event_count == LAB06_EVENT_CAPACITY);
    assert(session.queue_overflows == 1);
    assert(session.terminal_total == 1);
    assert(session.last_result.operation_id == 99);
    assert(session.last_result.terminal == LAB06_TERMINAL_FAILURE);
    assert(strcmp(session.last_result.reason, "overflow") == 0);
    assert(session.last_result.terminal_count == 1);
    uint32_t id;
    for (uint32_t expected = 1; expected <= LAB06_EVENT_CAPACITY; ++expected) {
        assert(lab06_event_queue_pop(&session, &id) && id == expected);
        assert(session.terminal_total == expected + 1);
        assert(session.last_result.operation_id == expected);
        assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
        assert(strcmp(session.last_result.reason, "queue_dequeued") == 0);
        assert(session.last_result.terminal_count == 1);
    }
    assert(!lab06_event_queue_pop(&session, &id));
    assert(session.terminal_total == LAB06_EVENT_CAPACITY + 1);
    assert(session.terminal_ids_count == LAB06_EVENT_CAPACITY + 1);
}

static void test_lab06_event_queue_terminals(void)
{
    lab06_session_t session;
    lab06_session_init(&session, 1, LAB06_STAGE_ROSTER);
    uint32_t id = 0;

    assert(lab06_event_queue_push(&session, 10));
    assert(session.event_count == 1 && session.terminal_total == 0);
    assert(!lab06_event_queue_pop(&session, NULL));
    assert(session.event_count == 1 && session.terminal_total == 0);
    assert(lab06_event_queue_pop(&session, &id) && id == 10);
    assert(session.event_count == 0 && session.terminal_total == 1);
    assert(session.last_result.operation_id == 10);
    assert(session.last_result.terminal == LAB06_TERMINAL_SUCCESS);
    assert(strcmp(session.last_result.reason, "queue_dequeued") == 0);
    assert(session.last_result.terminal_count == 1);

    assert(!lab06_event_queue_push(&session, 10));
    assert(!lab06_event_queue_push(&session, 9));
    assert(session.event_count == 0 && session.terminal_total == 1);
    assert(session.operation_id_watermark == 10);
    assert(session.queue_overflows == 0);
}

int main(int argc, char **argv)
{
    assert(argc == 2);
    if (strcmp(argv[1], "single_writer") == 0) test_lab06_single_writer();
    else if (strcmp(argv[1], "terminal_id_reuse") == 0) test_lab06_terminal_operation_id_reuse();
    else if (strcmp(argv[1], "geometry") == 0) test_lab06_geometry();
    else if (strcmp(argv[1], "idempotent_enter") == 0) test_lab06_idempotent_enter();
    else if (strcmp(argv[1], "data_during_exit") == 0) test_lab06_data_during_exit();
    else if (strcmp(argv[1], "stale_panel") == 0) test_lab06_stale_panel();
    else if (strcmp(argv[1], "cancel") == 0) test_lab06_cancel();
    else if (strcmp(argv[1], "cancel_schedule") == 0) test_lab06_cancel_schedule();
    else if (strcmp(argv[1], "level2_control") == 0) test_lab06_level2_control();
    else if (strcmp(argv[1], "pending_capacity") == 0) test_lab06_pending_capacity();
    else if (strcmp(argv[1], "data_levels") == 0) test_lab06_data_levels();
    else if (strcmp(argv[1], "cycle") == 0) test_lab06_cycle();
    else if (strcmp(argv[1], "overflow") == 0) test_lab06_event_overflow();
    else if (strcmp(argv[1], "queue_terminals") == 0) test_lab06_event_queue_terminals();
    else return 2;
    return 0;
}
