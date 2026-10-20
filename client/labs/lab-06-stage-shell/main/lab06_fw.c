#include "lab06_fw.h"

#include "lab06_model.h"
#include "lab06_cancel_schedule.h"
#include "esp_heap_caps.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"

#include <inttypes.h>
#include <stdatomic.h>
#include <stdio.h>
#include <string.h>

#define LAB06_QUEUE_CAPACITY 8U
#define LAB06_TIMER_PERIOD_MS 20U
#define LAB06_ANIMATION_US 400000LL
#define LAB06_CANCEL_PROBE_AT_US 200000LL
#define LAB06_LIVE_WIDGETS_EXPECTED 19U

typedef enum {
    UI_EVENT_PREVIOUS = 1,
    UI_EVENT_NEXT,
    UI_EVENT_BADGE,
    UI_EVENT_DATA_DURING_EXIT,
    UI_EVENT_CANCEL,
    UI_EVENT_STALE_CALLBACK
} ui_event_kind_t;

typedef struct {
    uint32_t publication_id;
    ui_event_kind_t kind;
    uint32_t generation;
} ui_event_t;

typedef enum {
    SCRIPT_IDLE = 0,
    SCRIPT_CYCLE_START,
    SCRIPT_CYCLE_PREP_WAIT,
    SCRIPT_CYCLE_WAIT,
    SCRIPT_CYCLE_RETURN_WAIT,
    SCRIPT_DATA,
    SCRIPT_DATA_EXIT_START,
    SCRIPT_DATA_EXIT_WAIT,
    SCRIPT_LEVEL2,
    SCRIPT_DESTROY_START,
    SCRIPT_DESTROY_WAIT,
    SCRIPT_CANCEL_START,
    SCRIPT_CANCEL_MID,
    SCRIPT_CANCEL_WAIT,
    SCRIPT_PLACEHOLDER,
    SCRIPT_DONE,
    SCRIPT_FAILED
} script_state_t;

typedef struct {
    lv_obj_t *root;
    lv_obj_t *data_label;
    uint32_t generation;
} panel_handle_t;

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *upper_slot;
    lv_obj_t *lower_slot;
    lv_obj_t *transition_cover;
    lv_obj_t *cover_upper;
    lv_obj_t *cover_lower;
    lv_obj_t *status;
    lv_obj_t *scenario_status;
    lv_obj_t *shell_data_label;
    lv_timer_t *shell_timer;
    QueueHandle_t queue;
    lab06_session_t session;
    panel_handle_t panels[2];
    panel_handle_t retired_panels[2];
    panel_handle_t staged_panels[2];
    panel_handle_t cover_panels[2];
    bool scripted;
    bool initialized;
    bool transitioning;
    bool transition_reverse;
    bool cancelling;
    bool transition_complete;
    lab06_cancel_control_t manual_cancel;
    bool pending_exit_update;
    bool transition_is_scenario_operation;
    int64_t transition_started_us;
    int64_t transition_duration_us;
    int32_t transition_from_y;
    int32_t transition_to_y;
    int32_t transition_origin_y;
    uint32_t active_operation_id;
    uint32_t last_publication_id;
    uint32_t operation_sequence;
    uint32_t stale_callback_generation;
    uint32_t pending_exit_generation;
    uint32_t pending_exit_data_operation_id;
    lab06_terminal_t pending_exit_data_terminal;
    uint32_t queue_overflow_count;
    uint32_t stale_handle_uses;
    uint32_t operation_count;
    uint32_t panel_build_us;
    uint32_t animation_us;
    uint32_t peak_panel_widgets;
    uint32_t cycle_stage;
    uint32_t cycle_index;
    uint32_t cycle_timer_start_count;
    uint32_t scenario_pass_count;
    uint32_t scenario_fail_count;
    uint32_t data_revision;
    uint32_t trace_counter;
    uint32_t last_scenario_operation;
    uint32_t scenario_operation_count;
    lab06_stage_t operation_stage_before;
    script_state_t script_state;
    char mode[12];
    char active_action[32];
    char terminal_reason[24];
} lab06_fw_t;

static lab06_fw_t s_fw;
static atomic_uint s_publication_sequence = ATOMIC_VAR_INIT(1U);
static atomic_uint s_synthetic_revision = ATOMIC_VAR_INIT(0U);
static atomic_uint s_publisher_overflows = ATOMIC_VAR_INIT(0U);
static uint32_t count_live_panel_widgets(void);

/* LVGL exposes its live timer list through lv_timer_get_next(). Count the
 * objects owned by this shell instead of trusting the model counter or a
 * retained pointer, either of which could outlive a deleted timer object. */
static uint32_t count_live_shell_timer_objects(void)
{
    uint32_t count = 0U;
    for (lv_timer_t *timer = lv_timer_get_next(NULL); timer != NULL;
         timer = lv_timer_get_next(timer)) {
        if (lv_timer_get_user_data(timer) == &s_fw) count++;
    }
    return count;
}

static const char *stage_name(lab06_stage_t stage)
{
    static const char *const names[LAB06_STAGE_COUNT] = {
        "roster", "pin", "hangout", "inbox", "record", "receipt"};
    return (unsigned)stage < LAB06_STAGE_COUNT ? names[stage] : "invalid";
}

static const char *scenario_name(script_state_t state)
{
    switch (state) {
    case SCRIPT_CYCLE_START:
    case SCRIPT_CYCLE_PREP_WAIT:
    case SCRIPT_CYCLE_WAIT:
    case SCRIPT_CYCLE_RETURN_WAIT: return "H-cycle";
    case SCRIPT_DATA: return "H-data";
    case SCRIPT_DATA_EXIT_START:
    case SCRIPT_DATA_EXIT_WAIT: return "H-data-during-exit";
    case SCRIPT_LEVEL2: return "H-level2-control";
    case SCRIPT_DESTROY_START:
    case SCRIPT_DESTROY_WAIT: return "H-destroyed";
    case SCRIPT_CANCEL_START:
    case SCRIPT_CANCEL_MID:
    case SCRIPT_CANCEL_WAIT: return "H-cancel";
    case SCRIPT_PLACEHOLDER: return "H-placeholder";
    default: return "manual";
    }
}

static uint32_t next_operation_id(void)
{
    return ++s_fw.operation_sequence;
}

/* The atomic id is used only by publishers. The session owner reserves model
 * operation ids in its own context; workers never read a panel or session. */
static uint32_t next_publication_id(void)
{
    return atomic_fetch_add_explicit(&s_publication_sequence, 1U,
                                     memory_order_relaxed);
}

static void trace_terminal(const char *scenario, uint32_t operation_id,
                           const char *terminal, const char *reason,
                           uint8_t update_level, uint8_t placeholder,
                           uint8_t old_panel_text_visible)
{
    const uint32_t slot_live = count_live_panel_widgets();
    printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"scenario_terminal\","
           "\"scenario\":\"%s\",\"class\":\"%s\",\"operation_id\":%" PRIu32
           ",\"generation\":%" PRIu32 ",\"stage_before\":\"%s\","
           "\"stage_after\":\"%s\",\"terminal\":\"%s\","
           "\"terminal_count\":1,\"reason\":\"%s\",\"update_level\":%u,"
           "\"operation_count\":%" PRIu32 ",\"queue_overflow\":%" PRIu32
           ",\"stale_handle_use\":%" PRIu32 ",\"widget_live\":%u,"
           "\"timer_count\":%u,\"anim_us\":%" PRIu32 ","
           "\"build_us\":%" PRIu32 ",\"placeholder\":%u,"
           "\"old_panel_text_visible\":%u}\n",
           scenario, strcmp(scenario, "H-placeholder") == 0 ? "exploratory" : "decided",
           operation_id, s_fw.session.next_generation, stage_name(s_fw.operation_stage_before),
           stage_name(s_fw.session.stage), terminal, reason, update_level,
           s_fw.operation_count, s_fw.queue_overflow_count, s_fw.stale_handle_uses,
           (unsigned)slot_live, (unsigned)count_live_shell_timer_objects(), s_fw.animation_us,
           s_fw.panel_build_us, placeholder, old_panel_text_visible);
}

/* Stage changes always keep their own evidence. Some transitions prepare a
 * later scenario operation and therefore must not be counted as that
 * scenario's terminal. */
static void trace_transition_terminal(uint32_t operation_id,
                                      const char *terminal, const char *reason,
                                      uint8_t update_level)
{
    printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"transition_terminal\","
           "\"operation_id\":%" PRIu32 ",\"generation\":%" PRIu32 ","
           "\"stage_before\":\"%s\",\"stage_after\":\"%s\","
           "\"terminal\":\"%s\",\"terminal_count\":1,\"reason\":\"%s\","
           "\"update_level\":%u,\"widget_live\":%u,\"timer_count\":%u,"
           "\"anim_us\":%" PRIu32 "}\n",
           operation_id, s_fw.session.next_generation,
           stage_name(s_fw.operation_stage_before), stage_name(s_fw.session.stage),
           terminal, reason, update_level, (unsigned)count_live_panel_widgets(),
           (unsigned)count_live_shell_timer_objects(), s_fw.animation_us);
}

static uint32_t count_tree(lv_obj_t *obj)
{
    if (obj == NULL) return 0U;
    uint32_t count = 1U;
    const uint32_t children = (uint32_t)lv_obj_get_child_count(obj);
    for (uint32_t i = 0; i < children; ++i)
        count += count_tree(lv_obj_get_child(obj, (int32_t)i));
    return count;
}

static uint32_t count_panel_widgets(void)
{
    return count_tree(s_fw.panels[0].root) + count_tree(s_fw.panels[1].root) +
           count_tree(s_fw.retired_panels[0].root) + count_tree(s_fw.retired_panels[1].root) +
           count_tree(s_fw.staged_panels[0].root) + count_tree(s_fw.staged_panels[1].root);
}

static uint32_t count_live_panel_widgets(void)
{
    return count_panel_widgets() + count_tree(s_fw.cover_panels[0].root) +
           count_tree(s_fw.cover_panels[1].root);
}

static void record_resource_sample(const char *reason)
{
    lv_mem_monitor_t monitor = {0};
    lv_mem_monitor(&monitor);
    const size_t free_internal = heap_caps_get_free_size(MALLOC_CAP_INTERNAL);
    const size_t largest_internal = heap_caps_get_largest_free_block(MALLOC_CAP_INTERNAL);
    const size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
    const size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
    printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"resource_sample\","
           "\"reason\":\"%s\",\"ledger_a\":\"lvgl_pool\","
           "\"lvgl_total_bytes\":%u,\"lvgl_free_bytes\":%u,"
           "\"lvgl_largest_free_bytes\":%u,\"lvgl_high_water_pct\":%u,"
           "\"ledger_b\":\"heap_caps\",\"internal_free_bytes\":%zu,"
           "\"internal_largest_free_bytes\":%zu,\"psram_free_bytes\":%zu,"
           "\"psram_largest_free_bytes\":%zu,\"provenance\":\"lv_mem_monitor+heap_caps;device\"}\n",
           reason, (unsigned)monitor.total_size, (unsigned)monitor.free_size,
           (unsigned)monitor.free_biggest_size, (unsigned)monitor.used_pct,
           free_internal, largest_internal, free_psram, largest_psram);
}

static void update_status(const char *action, const char *reason)
{
    (void)snprintf(s_fw.active_action, sizeof(s_fw.active_action), "%s", action);
    (void)snprintf(s_fw.terminal_reason, sizeof(s_fw.terminal_reason), "%s", reason);
    if (s_fw.status == NULL || s_fw.scenario_status == NULL) return;
    char line[112];
    const char *scenario = scenario_name(s_fw.script_state);
    if (strcmp(scenario, "manual") == 0) scenario = action;
    const char *terminal = "pending";
    if (strcmp(reason, "stage_changed") == 0 || strcmp(reason, "panel_data_applied") == 0 ||
        strcmp(reason, "ready") == 0 || strcmp(reason, "complete") == 0)
        terminal = "success";
    else if (strcmp(reason, "cancelled") == 0) terminal = "cancelled";
    else if (strcmp(reason, "stale_panel") == 0) terminal = "stale";
    else if (strcmp(reason, "overflow") == 0 || strcmp(reason, "level2_on_data") == 0 ||
             strcmp(reason, "scenario") == 0)
        terminal = "failure";
    if (strcmp(scenario, "H-cycle") == 0)
        (void)snprintf(line, sizeof(line), "H-cycle %" PRIu32 "/20\n%s:%s",
                       s_fw.cycle_index, terminal, reason);
    else
        (void)snprintf(line, sizeof(line), "%s\n%s:%s", scenario, terminal, reason);
    lv_label_set_text(s_fw.status, line);
    /* Keep the on-screen manual instructions present; scenario/action details
     * live in the upper panel status area. */
}

static void set_label(lv_obj_t *label, const char *text, int32_t x, int32_t y,
                      int32_t width, int32_t height, uint32_t color, uint32_t size)
{
    lv_label_set_text(label, text);
    lv_obj_set_pos(label, x, y);
    lv_obj_set_size(label, width, height);
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0);
    (void)size;
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    lv_label_set_long_mode(label, LV_LABEL_LONG_MODE_DOTS);
}

static void publish_event(ui_event_kind_t kind, uint32_t generation)
{
    ui_event_t event = { .publication_id = next_publication_id(),
                         .kind = kind, .generation = generation };
    if (s_fw.queue == NULL || xQueueSend(s_fw.queue, &event, 0) != pdTRUE) {
        s_fw.queue_overflow_count = atomic_fetch_add_explicit(
            &s_publisher_overflows, 1U, memory_order_relaxed) + 1U;
        printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"queue_overflow\","
               "\"publication_id\":%" PRIu32 ",\"queue_overflow\":%" PRIu32
               ",\"terminal\":\"failure\",\"terminal_count\":1,\"reason\":\"overflow\"}\n",
               event.publication_id, s_fw.queue_overflow_count);
        update_status(s_fw.active_action, "overflow");
        return;
    }
    s_fw.last_publication_id = event.publication_id;
}

static void click_event(lv_event_t *event)
{
    /* The scripted lifecycle owns the model while it runs. Ignore physical
     * taps so queued operator input cannot change its transitions or metrics. */
    if (s_fw.scripted) return;
    const uintptr_t packed = (uintptr_t)lv_event_get_user_data(event);
    const ui_event_kind_t kind = (ui_event_kind_t)(packed & 0xffU);
    const uint32_t generation = (uint32_t)(packed >> 8U);
    publish_event(kind, generation);
}

static lv_obj_t *make_button(lv_obj_t *parent, const char *text, int32_t x,
                             int32_t y, int32_t width, int32_t height,
                             ui_event_kind_t kind, uint32_t generation,
                             uint32_t color)
{
    lv_obj_t *button = lv_button_create(parent);
    if (button == NULL) return NULL;
    lv_obj_set_pos(button, x, y);
    lv_obj_set_size(button, width, height);
    lv_obj_set_style_bg_color(button, lv_color_hex(color), LV_STATE_DEFAULT);
    lv_obj_set_style_radius(button, 4, 0);
    lv_obj_set_style_border_width(button, 1, 0);
    lv_obj_set_style_border_color(button, lv_color_hex(0x9DB8C9), 0);
    lv_obj_add_event_cb(button, click_event, LV_EVENT_CLICKED,
                        (void *)(((uintptr_t)generation << 8U) | (uintptr_t)kind));
    lv_obj_t *label = lv_label_create(button);
    if (label == NULL) return NULL;
    lv_label_set_text(label, text);
    lv_obj_center(label);
    lv_obj_set_style_text_color(label, lv_color_hex(0xFFFFFF), 0);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_16, 0);
    return button;
}

static const char *panel_description(lab06_stage_t stage)
{
    static const char *const descriptions[LAB06_STAGE_COUNT] = {
        "Synthetic roster · rev 0", "PIN entry mock · no verification",
        "Hangout is empty", "Inbox placeholder · exploratory",
        "Record placeholder · no capture", "Receipt placeholder · exploratory"};
    return (unsigned)stage < LAB06_STAGE_COUNT ? descriptions[stage] : "Invalid stage";
}

static void delete_panel_handles(panel_handle_t handles[2])
{
    /* Handles and generations are cleared before LVGL objects are destroyed. */
    lv_obj_t *upper = handles[LAB06_SLOT_UPPER].root;
    lv_obj_t *lower = handles[LAB06_SLOT_LOWER].root;
    handles[0] = (panel_handle_t){0};
    handles[1] = (panel_handle_t){0};
    if (upper != NULL) lv_obj_delete(upper);
    if (lower != NULL) lv_obj_delete(lower);
}

static bool mount_panels(lab06_stage_t stage, panel_handle_t handles[2],
                         lv_obj_t *upper_parent, lv_obj_t *lower_parent)
{
    const int64_t started = esp_timer_get_time();
    lv_obj_t *upper = lv_obj_create(upper_parent);
    lv_obj_t *lower = lv_obj_create(lower_parent);
    if (upper == NULL || lower == NULL) {
        if (upper != NULL) lv_obj_delete(upper);
        if (lower != NULL) lv_obj_delete(lower);
        return false;
    }
    lv_obj_set_size(upper, 320, 80);
    lv_obj_set_pos(upper, 0, 0);
    lv_obj_set_style_bg_color(upper, lv_color_hex(0x18232B), 0);
    lv_obj_set_style_border_width(upper, 0, 0);
    lv_obj_set_style_pad_all(upper, 0, 0);
    lv_obj_set_scrollable(upper, false);
    lv_obj_set_size(lower, 320, 160);
    lv_obj_set_pos(lower, 0, upper_parent == lower_parent ? 80 : 0);
    lv_obj_set_style_bg_color(lower, lv_color_hex(0x202D36), 0);
    lv_obj_set_style_border_width(lower, 0, 0);
    lv_obj_set_style_pad_all(lower, 0, 0);
    lv_obj_set_scrollable(lower, false);

    lv_obj_t *title = lv_label_create(upper);
    lv_obj_t *status = lv_label_create(upper);
    if (title == NULL || status == NULL) {
        lv_obj_delete(upper);
        lv_obj_delete(lower);
        return false;
    }
    const char *title_text = stage_name(stage);
    char stage_title[40];
    (void)snprintf(stage_title, sizeof(stage_title), "LAB-06  /  %s", title_text);
    set_label(title, stage_title, 64, 12, 192, 18, 0xFFFFFF, 16);
    set_label(status, "manual\nready:ready", 64, 30, 192, 38, 0xB7CBD7, 16);
    lv_label_set_long_mode(status, LV_LABEL_LONG_MODE_WRAP);
    lv_obj_t *previous = make_button(upper, "<", 12, 16, 48, 48,
                                     UI_EVENT_PREVIOUS, s_fw.session.panels[0].generation, 0x354B58);
    lv_obj_t *next = make_button(upper, ">", 260, 16, 48, 48,
                                 UI_EVENT_NEXT, s_fw.session.panels[0].generation, 0x354B58);
    if (previous == NULL || next == NULL) {
        lv_obj_delete(upper);
        lv_obj_delete(lower);
        return false;
    }
    lv_obj_t *description = lv_label_create(lower);
    lv_obj_t *data_label = lv_label_create(lower);
    lv_obj_t *instructions = lv_label_create(lower);
    if (description == NULL || data_label == NULL || instructions == NULL) {
        lv_obj_delete(upper);
        lv_obj_delete(lower);
        return false;
    }
    set_label(description, panel_description(stage), 12, 12, 296, 18, 0xFFFFFF, 16);
    set_label(data_label, "Badge 0 · Progress 0/3 · Roster rev 0", 12, 31,
              296, 18, 0xD4E6D2, 16);
    set_label(instructions,
              "CANCEL PROBE · 2 taps, then wait\nCancel → Exit+Data → auto reverse",
              12, 49, 296, 35, 0xFFF0B3, 16);
    lv_label_set_long_mode(instructions, LV_LABEL_LONG_MODE_WRAP);
    /* Give the two-step manual cancellation probe the primary control row.
     * The other live lab actions remain available below it and identify
     * themselves as optional so the intended two taps are obvious. */
    lv_obj_t *cancel = make_button(lower, "1  Cancel", 12, 88, 145, 40,
                                   UI_EVENT_CANCEL, s_fw.session.panels[1].generation, 0x754343);
    lv_obj_t *during = make_button(lower, "2  Exit+Data", 163, 88, 145, 40,
                                   UI_EVENT_DATA_DURING_EXIT, s_fw.session.panels[1].generation, 0x76552F);
    lv_obj_t *badge = make_button(lower, "Badge · optional", 12, 132, 145, 27,
                                  UI_EVENT_BADGE, s_fw.session.panels[1].generation, 0x354A3B);
    lv_obj_t *stale = make_button(lower, "Stale · optional", 163, 132, 145, 27,
                                  UI_EVENT_STALE_CALLBACK, s_fw.session.panels[1].generation, 0x403A50);
    if (badge == NULL || during == NULL || cancel == NULL || stale == NULL) {
        lv_obj_delete(upper);
        lv_obj_delete(lower);
        return false;
    }
    handles[0] = (panel_handle_t){.root = upper, .data_label = NULL,
                                   .generation = s_fw.session.panels[0].generation};
    handles[1] = (panel_handle_t){.root = lower, .data_label = data_label,
                                   .generation = s_fw.session.panels[1].generation};
    s_fw.panel_build_us = (uint32_t)(esp_timer_get_time() - started);
    const uint32_t actual = count_tree(upper) + count_tree(lower);
    if (actual > s_fw.peak_panel_widgets) s_fw.peak_panel_widgets = actual;
    return actual == LAB06_LIVE_WIDGETS_EXPECTED;
}

static bool mount_stage(lab06_stage_t stage)
{
    delete_panel_handles(s_fw.retired_panels);
    delete_panel_handles(s_fw.staged_panels);
    delete_panel_handles(s_fw.panels);
    lv_obj_clean(s_fw.upper_slot);
    lv_obj_clean(s_fw.lower_slot);
    if (!mount_panels(stage, s_fw.panels, s_fw.upper_slot, s_fw.lower_slot))
        return false;
    s_fw.status = lv_obj_get_child(s_fw.panels[0].root, 1);
    s_fw.scenario_status = lv_obj_get_child(s_fw.panels[1].root, 1);
    s_fw.shell_data_label = s_fw.panels[1].data_label;
    update_status(s_fw.active_action, s_fw.terminal_reason);
    return true;
}

static void recover_transition_mount_failure(const char *reason)
{
    const bool cancel_arm_cleared = lab06_cancel_control_clear(
        &s_fw.manual_cancel, s_fw.scripted);
    const uint32_t operation_id = s_fw.active_operation_id;
    const lab06_stage_t source = s_fw.session.transition_source;
    s_fw.operation_stage_before = source;
    const bool cancelled = lab06_cancel_stage_transition(&s_fw.session, operation_id);
    s_fw.operation_count++;
    lv_obj_t *cover = s_fw.transition_cover;
    s_fw.transition_cover = NULL;
    s_fw.transitioning = false;
    s_fw.pending_exit_update = false;
    s_fw.pending_exit_generation = 0U;
    s_fw.status = NULL;
    s_fw.scenario_status = NULL;
    s_fw.shell_data_label = NULL;
    delete_panel_handles(s_fw.cover_panels);
    if (cover != NULL) lv_obj_delete(cover);
    (void)mount_stage(s_fw.session.stage);
    trace_terminal(scenario_name(s_fw.script_state), operation_id,
                   cancelled ? "cancelled" : "failure", reason,
                   LAB06_UPDATE_SCENE, 0, 0);
    update_status(s_fw.active_action, cancel_arm_cleared ?
                  "cancel cleared; retry" : reason);
    if (s_fw.scripted) {
        s_fw.scenario_fail_count++;
        s_fw.script_state = SCRIPT_FAILED;
    } else {
        printf("LAB06_RUNTIME_FAILED reason=%s\n", reason);
    }
}

static void mark_scenario(bool passed)
{
    if (passed) s_fw.scenario_pass_count++;
    else s_fw.scenario_fail_count++;
    if (!passed) s_fw.script_state = SCRIPT_FAILED;
}

static void trace_summary(void)
{
    printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"trace_summary\","
           "\"scenario\":\"H-placeholder\",\"class\":\"exploratory\","
           "\"placeholder\":1,\"experiment_inputs\":[\"H-cycle\",\"H-data\","
           "\"H-data-during-exit\",\"H-level2-control\",\"H-destroyed\",\"H-cancel\"],"
           "\"scenario_pass_count\":%" PRIu32 ",\"scenario_fail_count\":%" PRIu32
           ",\"operation_count\":%" PRIu32 ",\"queue_overflow\":%" PRIu32
           ",\"stale_handle_use\":%" PRIu32 ",\"peak_panel_widgets\":%" PRIu32
           ",\"timer_count\":%u,\"performance\":\"UNMEASURED\"}\n",
           s_fw.scenario_pass_count, s_fw.scenario_fail_count, s_fw.operation_count,
           s_fw.queue_overflow_count, s_fw.stale_handle_uses,
           s_fw.peak_panel_widgets, (unsigned)count_live_shell_timer_objects());
    record_resource_sample("harness_done");
}

static uint32_t apply_data(lab06_slot_t slot, uint32_t generation,
                           const char *text, lab06_update_level_t level,
                           const char *scenario)
{
    const uint32_t operation_id = next_operation_id();
    s_fw.operation_stage_before = s_fw.session.stage;
    const bool applied = lab06_apply_panel_data(&s_fw.session, slot, generation,
                                                 operation_id, text, level);
    s_fw.operation_count++;
    const lab06_operation_result_t result = s_fw.session.last_result;
    const char *terminal = result.operation_id == operation_id ?
        (result.terminal == LAB06_TERMINAL_SUCCESS ? "success" :
         result.terminal == LAB06_TERMINAL_FAILURE ? "failure" :
         result.terminal == LAB06_TERMINAL_STALE ? "stale" : "failure") : "failure";
    const char *reason = result.operation_id == operation_id && result.reason != NULL ?
        result.reason : "model_rejected";
    if (applied && slot == LAB06_SLOT_LOWER && s_fw.panels[slot].data_label != NULL)
        lv_label_set_text(s_fw.panels[slot].data_label, text);
    else if (applied && slot == LAB06_SLOT_UPPER && s_fw.shell_data_label != NULL)
        lv_label_set_text(s_fw.shell_data_label, text);
    trace_terminal(scenario, operation_id, terminal, reason, (uint8_t)level, 0,
                   0U);
    update_status(scenario, reason);
    return operation_id;
}

static bool begin_transition(lab06_stage_t destination, const char *action,
                             bool during_data)
{
    if (s_fw.transitioning) return false;
    if (destination >= LAB06_STAGE_COUNT) {
        if (lab06_cancel_control_clear(&s_fw.manual_cancel, s_fw.scripted))
            update_status(action, "cancel cleared; retry");
        return false;
    }
    const uint32_t operation_id = next_operation_id();
    s_fw.operation_stage_before = s_fw.session.stage;
    s_fw.active_operation_id = operation_id;
    s_fw.transition_is_scenario_operation =
        strcmp(action, "H-cycle enter") == 0 || strcmp(action, "H-cancel") == 0;
    s_fw.operation_count++;
    s_fw.scenario_operation_count++;
    const uint32_t old_generation = s_fw.panels[LAB06_SLOT_LOWER].generation;
    const bool reverse = destination == (lab06_stage_t)((s_fw.session.stage +
        LAB06_STAGE_COUNT - 1U) % LAB06_STAGE_COUNT);
    s_fw.shell_data_label = NULL;
    if (!lab06_begin_stage_transition(&s_fw.session, s_fw.session.owner_id,
                                     destination, operation_id)) {
        const bool cancel_arm_cleared = lab06_cancel_control_clear(
            &s_fw.manual_cancel, s_fw.scripted);
        const lab06_operation_result_t result = s_fw.session.last_result;
        const char *reason = result.reason == NULL ? "begin_failed" : result.reason;
        if (s_fw.transition_is_scenario_operation)
            trace_terminal(scenario_name(s_fw.script_state), operation_id, "failure",
                           reason, LAB06_UPDATE_SCENE, 0, 0);
        else
            trace_transition_terminal(operation_id, "failure", reason,
                                      LAB06_UPDATE_SCENE);
        update_status(action, cancel_arm_cleared ?
                      "cancel cleared; retry" : reason);
        return false;
    }
    /* Invalidate panel pointers before the old tree can be retired. The cover
     * is a transient foreground containing the incoming stage pair. */
    s_fw.retired_panels[0] = s_fw.panels[0];
    s_fw.retired_panels[1] = s_fw.panels[1];
    s_fw.panels[0] = (panel_handle_t){0};
    s_fw.panels[1] = (panel_handle_t){0};
    s_fw.operation_stage_before = s_fw.session.transition_source;
    if (s_fw.status != NULL) s_fw.status = NULL;
    if (s_fw.scenario_status != NULL) s_fw.scenario_status = NULL;
    s_fw.transition_cover = lv_obj_create(s_fw.screen);
    if (s_fw.transition_cover == NULL) {
        recover_transition_mount_failure("transition_cover_alloc");
        return false;
    }
    lv_obj_set_size(s_fw.transition_cover, 320, 240);
    const int32_t origin_y = reverse ? 0 : 240;
    lv_obj_set_pos(s_fw.transition_cover, 0, origin_y);
    lv_obj_set_style_bg_color(s_fw.transition_cover, lv_color_hex(0x101820), 0);
    lv_obj_set_style_border_width(s_fw.transition_cover, 0, 0);
    lv_obj_set_style_pad_all(s_fw.transition_cover, 0, 0);
    lv_obj_set_scrollable(s_fw.transition_cover, false);
    s_fw.cover_panels[0] = (panel_handle_t){0};
    s_fw.cover_panels[1] = (panel_handle_t){0};
    const lab06_stage_t cover_stage = reverse ? s_fw.session.transition_source : destination;
    if (!mount_panels(cover_stage, s_fw.cover_panels, s_fw.transition_cover,
                      s_fw.transition_cover)) {
        recover_transition_mount_failure("cover_panel_mount");
        return false;
    }
    /* Both slots are sibling modules; mount them at their fixed coordinates
     * inside the cover rather than moving the persistent shell split. */
    lv_obj_set_pos(s_fw.cover_panels[0].root, 0, 0);
    lv_obj_set_pos(s_fw.cover_panels[1].root, 0, 80);
    lv_obj_set_style_bg_color(s_fw.cover_panels[0].root, lv_color_hex(0x18232B), 0);
    lv_obj_set_style_bg_color(s_fw.cover_panels[1].root, lv_color_hex(0x202D36), 0);
    if (reverse) {
        /* The current stage is the moving foreground. Prepare the previous
         * stage underneath it, then reveal that pair as the cover moves down. */
        delete_panel_handles(s_fw.retired_panels);
        lv_obj_clean(s_fw.upper_slot);
        lv_obj_clean(s_fw.lower_slot);
        s_fw.staged_panels[0] = (panel_handle_t){0};
        s_fw.staged_panels[1] = (panel_handle_t){0};
        if (!mount_panels(destination, s_fw.staged_panels,
                          s_fw.upper_slot, s_fw.lower_slot)) {
            recover_transition_mount_failure("underlay_panel_mount");
            return false;
        }
    }
    s_fw.peak_panel_widgets = count_live_panel_widgets() > s_fw.peak_panel_widgets ?
        count_live_panel_widgets() : s_fw.peak_panel_widgets;
    s_fw.transitioning = true;
    s_fw.transition_reverse = reverse;
    s_fw.cancelling = false;
    s_fw.transition_started_us = esp_timer_get_time();
    s_fw.transition_duration_us = LAB06_ANIMATION_US;
    s_fw.transition_from_y = origin_y;
    s_fw.transition_to_y = reverse ? 240 : 0;
    s_fw.transition_origin_y = origin_y;
    s_fw.transition_complete = false;
    s_fw.manual_cancel.transition_scheduled =
        lab06_cancel_transition_started(&s_fw.manual_cancel, s_fw.scripted);
    (void)snprintf(s_fw.active_action, sizeof(s_fw.active_action), "%s", action);
    s_fw.status = lv_obj_get_child(s_fw.cover_panels[0].root, 1);
    s_fw.scenario_status = lv_obj_get_child(s_fw.cover_panels[1].root, 1);
    s_fw.shell_data_label = s_fw.cover_panels[1].data_label;
    update_status(action, s_fw.manual_cancel.transition_scheduled ?
                  "Cancel armed; reverse at 200 ms" : "transition");
    record_resource_sample("transition_start");
    if (during_data) {
        s_fw.pending_exit_update = true;
        s_fw.pending_exit_generation = old_generation;
        s_fw.pending_exit_data_operation_id = 0U;
        s_fw.pending_exit_data_terminal = LAB06_TERMINAL_NONE;
    }
    return true;
}

static void finish_transition(void)
{
    const int64_t now = esp_timer_get_time();
    s_fw.animation_us = (uint32_t)(now - s_fw.transition_started_us);
    const uint32_t old_operation = s_fw.active_operation_id;
    const bool was_cancelled = s_fw.cancelling;
    if (was_cancelled) {
        (void)lab06_cancel_stage_transition(&s_fw.session, old_operation);
    } else {
        (void)lab06_finish_stage_transition(&s_fw.session, old_operation);
    }
    const lab06_operation_result_t result = s_fw.session.last_result;
    s_fw.operation_count++;
    s_fw.transitioning = false;
    lab06_cancel_transition_finished(&s_fw.manual_cancel);
    lv_obj_t *finished_cover = s_fw.transition_cover;
    s_fw.transition_cover = NULL;
    s_fw.pending_exit_update = false;
    s_fw.pending_exit_generation = 0U;
    s_fw.status = NULL;
    s_fw.scenario_status = NULL;
    s_fw.shell_data_label = NULL;
    delete_panel_handles(s_fw.cover_panels);
    if (!mount_stage(s_fw.session.stage)) {
        if (finished_cover != NULL) lv_obj_delete(finished_cover);
        s_fw.script_state = SCRIPT_FAILED;
        return;
    }
    if (finished_cover != NULL) lv_obj_delete(finished_cover);
    if (result.operation_id == old_operation) {
        const char *terminal = was_cancelled ? "cancelled" : "success";
        const char *reason = result.reason == NULL ? "transition_done" : result.reason;
        if (s_fw.transition_is_scenario_operation)
            trace_terminal(scenario_name(s_fw.script_state), old_operation, terminal,
                           reason, LAB06_UPDATE_SCENE, 0, 0);
        else
            trace_transition_terminal(old_operation, terminal, reason,
                                      LAB06_UPDATE_SCENE);
    }
    s_fw.transition_is_scenario_operation = false;
    update_status(s_fw.active_action,
                  was_cancelled ? "cancelled" : "stage_changed");
    record_resource_sample(was_cancelled ? "transition_cancel" : "transition_done");
    s_fw.transition_complete = true;
}

static void start_reverse_cancel(void)
{
    if (!s_fw.transitioning || s_fw.cancelling) return;
    if (s_fw.transition_reverse) {
        (void)mount_stage(s_fw.session.transition_source);
        s_fw.status = lv_obj_get_child(s_fw.cover_panels[0].root, 1);
        s_fw.scenario_status = lv_obj_get_child(s_fw.cover_panels[1].root, 1);
        s_fw.shell_data_label = s_fw.cover_panels[1].data_label;
    }
    const int32_t current_y = lv_obj_get_y(s_fw.transition_cover);
    const int32_t remaining = current_y >= s_fw.transition_origin_y ?
        current_y - s_fw.transition_origin_y : s_fw.transition_origin_y - current_y;
    s_fw.cancelling = true;
    (void)lab06_cancel_control_clear(&s_fw.manual_cancel, s_fw.scripted);
    s_fw.transition_from_y = current_y;
    s_fw.transition_to_y = s_fw.transition_origin_y;
    s_fw.transition_started_us = esp_timer_get_time();
    s_fw.transition_duration_us = LAB06_ANIMATION_US * remaining / 240;
    if (s_fw.transition_duration_us < 20000) s_fw.transition_duration_us = 20000;
    update_status("H-cancel", "reverse");
}

static void process_ui_event(const ui_event_t *event)
{
    if (event == NULL) return;
    if (event->generation != 0U && event->kind != UI_EVENT_STALE_CALLBACK &&
        (event->kind != UI_EVENT_CANCEL || !s_fw.transitioning)) {
        const lab06_slot_t slot = event->kind == UI_EVENT_PREVIOUS ||
            event->kind == UI_EVENT_NEXT ? LAB06_SLOT_UPPER : LAB06_SLOT_LOWER;
        if (!s_fw.session.panels[slot].alive ||
            s_fw.session.panels[slot].generation != event->generation) {
            (void)apply_data(slot, event->generation, "Stale queued input",
                             LAB06_UPDATE_PROPERTY, "H-destroyed");
            return;
        }
    }
    switch (event->kind) {
    case UI_EVENT_PREVIOUS:
        if (!s_fw.transitioning) {
            const lab06_stage_t previous = (lab06_stage_t)((s_fw.session.stage + LAB06_STAGE_COUNT - 1U) % LAB06_STAGE_COUNT);
            (void)begin_transition(previous, "return", false);
        }
        break;
    case UI_EVENT_NEXT:
        if (!s_fw.transitioning) {
            const lab06_stage_t next = (lab06_stage_t)((s_fw.session.stage + 1U) % LAB06_STAGE_COUNT);
            (void)begin_transition(next, "next", false);
        }
        break;
    case UI_EVENT_BADGE:
        if (!s_fw.transitioning) {
            char text[48];
            (void)snprintf(text, sizeof(text), "Synthetic badge %" PRIu32,
                           ++s_fw.data_revision);
            (void)apply_data(LAB06_SLOT_UPPER, s_fw.panels[0].generation, text,
                             LAB06_UPDATE_PROPERTY, "H-data");
            (void)apply_data(LAB06_SLOT_LOWER, s_fw.panels[1].generation,
                             "Progress 1 of 3 · roster revision +1",
                             LAB06_UPDATE_SUBTREE, "H-data");
        }
        break;
    case UI_EVENT_DATA_DURING_EXIT: {
        const lab06_stage_t next = (lab06_stage_t)((s_fw.session.stage + 1U) % LAB06_STAGE_COUNT);
        (void)begin_transition(next, "H-data-during-exit", true);
        break;
    }
    case UI_EVENT_CANCEL:
        if (s_fw.transitioning) {
            /* Keep direct cancellation as a shortcut; the stable idle arm is
             * the reliable path because the moving cover hides this panel. */
            s_fw.manual_cancel.transition_scheduled = false;
            start_reverse_cancel();
        } else if (lab06_cancel_arm(&s_fw.manual_cancel, s_fw.scripted,
                                    s_fw.transitioning)) {
            update_status("manual", "Cancel armed");
        }
        break;
    case UI_EVENT_STALE_CALLBACK: {
        const uint32_t old_generation = event->generation;
        if (!s_fw.transitioning) {
            const lab06_stage_t next = (lab06_stage_t)((s_fw.session.stage + 1U) % LAB06_STAGE_COUNT);
            (void)begin_transition(next, "H-destroyed", false);
            /* Retain only an immutable generation, never the widget pointer. */
            s_fw.stale_callback_generation = old_generation;
        }
        break;
    }
    }
}

static void process_stale_callback(void)
{
    if (s_fw.stale_callback_generation == 0U || s_fw.transitioning) return;
    const uint32_t before_uses = s_fw.session.handle_uses;
    const uint32_t id = apply_data(LAB06_SLOT_LOWER,
                                   s_fw.stale_callback_generation,
                                   "Callback after panel destruction",
                                   LAB06_UPDATE_PROPERTY, "H-destroyed");
    const lab06_operation_result_t result = s_fw.session.last_result;
    const bool passed = result.operation_id == id &&
                        result.terminal == LAB06_TERMINAL_STALE &&
                        before_uses == s_fw.session.handle_uses;
    mark_scenario(passed);
    s_fw.stale_callback_generation = 0U;
}

static void script_tick(void)
{
    if (!s_fw.scripted || s_fw.script_state == SCRIPT_DONE ||
        s_fw.script_state == SCRIPT_FAILED) return;
    switch (s_fw.script_state) {
    case SCRIPT_IDLE:
        s_fw.cycle_timer_start_count = count_live_shell_timer_objects();
        if (s_fw.cycle_timer_start_count != 1U) {
            mark_scenario(false);
            break;
        }
        s_fw.script_state = SCRIPT_CYCLE_START;
        s_fw.cycle_stage = 0U;
        s_fw.cycle_index = 0U;
        break;
    case SCRIPT_CYCLE_START:
        if (s_fw.cycle_stage >= LAB06_STAGE_COUNT) {
            mark_scenario(s_fw.scenario_fail_count == 0U);
            s_fw.script_state = SCRIPT_DATA;
            break;
        }
        if (s_fw.cycle_index >= 20U) {
            s_fw.cycle_stage++;
            s_fw.cycle_index = 0U;
            break;
        }
        if (!s_fw.transitioning) {
            const lab06_stage_t target = (lab06_stage_t)s_fw.cycle_stage;
            const lab06_stage_t source = (lab06_stage_t)((s_fw.cycle_stage + LAB06_STAGE_COUNT - 1U) % LAB06_STAGE_COUNT);
            if (s_fw.session.stage != source) {
                if (begin_transition(source, "H-cycle setup", false))
                    s_fw.script_state = SCRIPT_CYCLE_PREP_WAIT;
            } else if (begin_transition(target, "H-cycle enter", false)) {
                s_fw.script_state = SCRIPT_CYCLE_WAIT;
            }
        }
        break;
    case SCRIPT_CYCLE_PREP_WAIT:
        if (s_fw.transition_complete) {
            s_fw.transition_complete = false;
            const lab06_stage_t target = (lab06_stage_t)s_fw.cycle_stage;
            if (begin_transition(target, "H-cycle enter", false))
                s_fw.script_state = SCRIPT_CYCLE_WAIT;
        }
        break;
    case SCRIPT_CYCLE_WAIT:
        if (s_fw.transition_complete) {
            s_fw.transition_complete = false;
            const bool live_ok = count_panel_widgets() == LAB06_LIVE_WIDGETS_EXPECTED;
            const uint32_t live_timer_count = count_live_shell_timer_objects();
            const bool timer_ok = s_fw.cycle_timer_start_count == 1U &&
                live_timer_count == s_fw.cycle_timer_start_count;
            mark_scenario(live_ok && timer_ok);
            if (!live_ok || !timer_ok) break;
            const lab06_stage_t source = (lab06_stage_t)((s_fw.cycle_stage + LAB06_STAGE_COUNT - 1U) % LAB06_STAGE_COUNT);
            if (!begin_transition(source, "H-cycle exit", false)) break;
            s_fw.script_state = SCRIPT_CYCLE_RETURN_WAIT;
        }
        break;
    case SCRIPT_CYCLE_RETURN_WAIT:
        if (s_fw.transition_complete) {
            s_fw.transition_complete = false;
            const bool live_ok = count_panel_widgets() == LAB06_LIVE_WIDGETS_EXPECTED;
            const uint32_t live_timer_count = count_live_shell_timer_objects();
            const bool timer_ok = s_fw.cycle_timer_start_count == 1U &&
                live_timer_count == s_fw.cycle_timer_start_count;
            mark_scenario(live_ok && timer_ok);
            if (!live_ok || !timer_ok) break;
            s_fw.cycle_index++;
            s_fw.script_state = SCRIPT_CYCLE_START;
        }
        break;
    case SCRIPT_DATA: {
        const uint32_t focused = s_fw.session.panels[0].focused_synthetic_id;
        const uint32_t id0 = apply_data(LAB06_SLOT_UPPER, s_fw.panels[0].generation,
            "Badge 1 · progress 1/3", LAB06_UPDATE_PROPERTY, "H-data");
        const bool property_ok = s_fw.session.last_result.operation_id == id0 &&
            s_fw.session.last_result.terminal == LAB06_TERMINAL_SUCCESS;
        const uint32_t id1 = apply_data(LAB06_SLOT_LOWER, s_fw.panels[1].generation,
            "Roster revision 1", LAB06_UPDATE_SUBTREE, "H-data");
        const bool subtree_ok = s_fw.session.last_result.operation_id == id1 &&
            s_fw.session.last_result.terminal == LAB06_TERMINAL_SUCCESS;
        const bool identity_ok = focused == s_fw.session.panels[0].focused_synthetic_id;
        mark_scenario(property_ok && subtree_ok && identity_ok);
        if (s_fw.script_state == SCRIPT_FAILED) break;
        s_fw.script_state = SCRIPT_DATA_EXIT_START;
        break;
    }
    case SCRIPT_DATA_EXIT_START: {
        const lab06_stage_t target = LAB06_STAGE_PIN;
        if (!s_fw.transitioning && begin_transition(target, "H-data-during-exit", true))
            s_fw.script_state = SCRIPT_DATA_EXIT_WAIT;
        break;
    }
    case SCRIPT_DATA_EXIT_WAIT:
        if (s_fw.transition_complete) {
            s_fw.transition_complete = false;
            const bool data_terminal_ok =
                s_fw.pending_exit_data_operation_id != 0U &&
                (s_fw.pending_exit_data_terminal == LAB06_TERMINAL_SUCCESS ||
                 s_fw.pending_exit_data_terminal == LAB06_TERMINAL_STALE);
            mark_scenario(s_fw.session.stage == LAB06_STAGE_PIN && data_terminal_ok);
            if (s_fw.script_state == SCRIPT_FAILED) break;
            s_fw.script_state = SCRIPT_LEVEL2;
        }
        break;
    case SCRIPT_LEVEL2: {
        const uint32_t id = apply_data(LAB06_SLOT_UPPER, s_fw.panels[0].generation,
            "Negative control", LAB06_UPDATE_SCENE, "H-level2-control");
        const lab06_operation_result_t result = s_fw.session.last_result;
        mark_scenario(result.operation_id == id && result.terminal == LAB06_TERMINAL_FAILURE &&
                      strcmp(result.reason, "level2_on_data") == 0);
        if (s_fw.script_state == SCRIPT_FAILED) break;
        s_fw.script_state = SCRIPT_DESTROY_START;
        break;
    }
    case SCRIPT_DESTROY_START:
        if (!s_fw.transitioning) {
            s_fw.stale_callback_generation = s_fw.panels[LAB06_SLOT_LOWER].generation;
            if (begin_transition(LAB06_STAGE_HANGOUT, "H-destroyed", false))
                s_fw.script_state = SCRIPT_DESTROY_WAIT;
        }
        break;
    case SCRIPT_DESTROY_WAIT:
        if (s_fw.transition_complete) {
            s_fw.transition_complete = false;
            process_stale_callback();
            if (s_fw.script_state == SCRIPT_FAILED) break;
            s_fw.script_state = SCRIPT_CANCEL_START;
        }
        break;
    case SCRIPT_CANCEL_START:
        if (!s_fw.transitioning && begin_transition(LAB06_STAGE_RECORD,
                                                     "H-cancel", false)) {
            s_fw.script_state = SCRIPT_CANCEL_MID;
        }
        break;
    case SCRIPT_CANCEL_MID:
        if (s_fw.transition_complete) {
            /* The cover completed before the scripted midpoint cancel. Emit
             * a decided failure instead of waiting forever for a terminal
             * that has already happened. */
            s_fw.transition_complete = false;
            mark_scenario(false);
            s_fw.script_state = SCRIPT_FAILED;
            break;
        }
        break;
    case SCRIPT_CANCEL_WAIT:
        if (s_fw.transition_complete) {
            s_fw.transition_complete = false;
            mark_scenario(s_fw.session.stage == LAB06_STAGE_HANGOUT &&
                          count_live_shell_timer_objects() == 1U);
            if (s_fw.script_state == SCRIPT_FAILED) break;
            s_fw.script_state = SCRIPT_PLACEHOLDER;
        }
        break;
    case SCRIPT_PLACEHOLDER:
        s_fw.operation_stage_before = s_fw.session.stage;
        trace_terminal("H-placeholder", next_operation_id(), "success", "synthetic_only",
                       LAB06_UPDATE_SUBTREE, 1, 0);
        mark_scenario(true);
        s_fw.script_state = s_fw.scenario_fail_count == 0U ? SCRIPT_DONE : SCRIPT_FAILED;
        break;
    case SCRIPT_DONE:
    case SCRIPT_FAILED:
        break;
    }
}

static void timer_tick(lv_timer_t *timer)
{
    (void)timer;
    s_fw.queue_overflow_count = atomic_load_explicit(&s_publisher_overflows,
                                                     memory_order_relaxed);
    ui_event_t event;
    if (s_fw.queue != NULL && xQueueReceive(s_fw.queue, &event, 0) == pdTRUE) {
        /* A consumer task owns all model and LVGL mutation. The queue payload is
         * immutable and contains no pointer to a panel or widget. */
        s_fw.operation_count++;
        process_ui_event(&event);
    }
    if (s_fw.transitioning && s_fw.transition_cover != NULL) {
        int64_t elapsed = esp_timer_get_time() - s_fw.transition_started_us;
        const bool scripted_cancel =
            s_fw.scripted && s_fw.script_state == SCRIPT_CANCEL_MID;
        const bool cancel_probe_pending = scripted_cancel ||
            (!s_fw.scripted && s_fw.manual_cancel.transition_scheduled);
        const lab06_animation_action_t action = lab06_animation_action(
            cancel_probe_pending,
            elapsed, LAB06_CANCEL_PROBE_AT_US, s_fw.transition_duration_us);
        if (action == LAB06_ANIMATION_CANCEL) {
            /* Choose reverse before this tick can run the normal completion
             * branch. Monotonic time keeps the probe deterministic even when
             * LVGL delivers this callback late. */
            start_reverse_cancel();
            if (scripted_cancel) s_fw.script_state = SCRIPT_CANCEL_WAIT;
            elapsed = esp_timer_get_time() - s_fw.transition_started_us;
        }
        if (s_fw.pending_exit_update && elapsed >= 160000LL) {
            const uint32_t operation_id = apply_data(LAB06_SLOT_LOWER, s_fw.pending_exit_generation,
                             "Synthetic update during exit", LAB06_UPDATE_SUBTREE,
                             "H-data-during-exit");
            if (s_fw.session.last_result.operation_id == operation_id) {
                s_fw.pending_exit_data_operation_id = operation_id;
                s_fw.pending_exit_data_terminal = s_fw.session.last_result.terminal;
            }
            s_fw.pending_exit_update = false;
        }
        const float fraction = s_fw.transition_duration_us <= 0 ? 1.0f :
            (float)elapsed / (float)s_fw.transition_duration_us;
        if (action == LAB06_ANIMATION_FINISH || fraction >= 1.0f) {
            lv_obj_set_y(s_fw.transition_cover, s_fw.transition_to_y);
            finish_transition();
        } else {
            const float eased = 1.0f - (1.0f - fraction) * (1.0f - fraction);
            const float y = (float)s_fw.transition_from_y +
                ((float)s_fw.transition_to_y - (float)s_fw.transition_from_y) * eased;
            lv_obj_set_y(s_fw.transition_cover, (int32_t)y);
        }
    }
    if (s_fw.stale_callback_generation != 0U && !s_fw.transitioning)
        process_stale_callback();
    script_tick();
    if (s_fw.scripted && s_fw.script_state == SCRIPT_DONE) {
        trace_summary();
        update_status("HARN. COMPLETE", "complete");
        printf("LAB06_HARNESS_DONE status=complete\n");
        s_fw.script_state = SCRIPT_IDLE;
        s_fw.scripted = false;
    } else if (s_fw.scripted && s_fw.script_state == SCRIPT_FAILED) {
        trace_summary();
        update_status("HARN. FAILED", "scenario");
        printf("LAB06_HARNESS_DONE status=failure reason=scenario\n");
        s_fw.script_state = SCRIPT_IDLE;
        s_fw.scripted = false;
    }
}

static void synthetic_worker(void *arg)
{
    (void)arg;
    for (;;) {
        vTaskDelay(pdMS_TO_TICKS(5000));
        ui_event_t event = {.publication_id = next_publication_id(),
                            .kind = UI_EVENT_BADGE, .generation = 0U};
        atomic_fetch_add_explicit(&s_synthetic_revision, 1U, memory_order_relaxed);
        if (s_fw.queue == NULL || xQueueSend(s_fw.queue, &event, 0) != pdTRUE) {
            /* Worker publication failure is observable; no widget/session
             * pointer crosses this queue boundary. */
            const uint32_t overflows = atomic_fetch_add_explicit(
                &s_publisher_overflows, 1U, memory_order_relaxed) + 1U;
            printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"queue_overflow\","
                   "\"publication_id\":%" PRIu32 ",\"queue_overflow\":%" PRIu32
                   ",\"terminal\":\"failure\",\"terminal_count\":1,"
                   "\"reason\":\"overflow\"}\n", event.publication_id, overflows);
        }
    }
}

bool lab06_fw_init(lv_obj_t *screen, const char *mode)
{
    if (screen == NULL || mode == NULL) return false;
    memset(&s_fw, 0, sizeof(s_fw));
    s_fw.screen = screen;
    s_fw.scripted = strcmp(mode, "scripted") == 0;
    (void)snprintf(s_fw.mode, sizeof(s_fw.mode), "%s", mode);
    (void)snprintf(s_fw.active_action, sizeof(s_fw.active_action), "Tap < or > to change stage");
    (void)snprintf(s_fw.terminal_reason, sizeof(s_fw.terminal_reason), "ready");
    s_fw.script_state = s_fw.scripted ? SCRIPT_IDLE : SCRIPT_DONE;
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);
    s_fw.queue = xQueueCreate(LAB06_QUEUE_CAPACITY, sizeof(ui_event_t));
    if (s_fw.queue == NULL) return false;
    if (s_fw.scripted) {
        ui_event_t probe = {.publication_id = 0U, .kind = UI_EVENT_BADGE, .generation = 0U};
        for (uint32_t accepted = 0U; accepted < LAB06_QUEUE_CAPACITY; ++accepted) {
            probe.publication_id = next_publication_id();
            if (xQueueSend(s_fw.queue, &probe, 0) != pdTRUE) return false;
        }
        probe.publication_id = next_publication_id();
        if (xQueueSend(s_fw.queue, &probe, 0) == pdTRUE) return false;
        s_fw.queue_overflow_count = atomic_fetch_add_explicit(
            &s_publisher_overflows, 1U, memory_order_relaxed) + 1U;
        printf("{\"schema\":\"lab06-trace/v1\",\"event\":\"queue_overflow\","
               "\"publication_id\":%" PRIu32 ",\"queue_overflow\":%" PRIu32
               ",\"terminal\":\"failure\",\"terminal_count\":1,"
               "\"reason\":\"overflow\",\"probe\":\"bounded_queue_capacity_8\"}\n",
               probe.publication_id, s_fw.queue_overflow_count);
        while (xQueueReceive(s_fw.queue, &probe, 0) == pdTRUE) { }
    }
    lab06_session_init(&s_fw.session, 1U, LAB06_STAGE_ROSTER);
    s_fw.upper_slot = lv_obj_create(screen);
    s_fw.lower_slot = lv_obj_create(screen);
    if (s_fw.upper_slot == NULL || s_fw.lower_slot == NULL) return false;
    lv_obj_set_size(s_fw.upper_slot, 320, 80);
    lv_obj_set_pos(s_fw.upper_slot, 0, 0);
    lv_obj_set_style_bg_opa(s_fw.upper_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_fw.upper_slot, 0, 0);
    lv_obj_set_style_pad_all(s_fw.upper_slot, 0, 0);
    lv_obj_set_scrollable(s_fw.upper_slot, false);
    lv_obj_set_size(s_fw.lower_slot, 320, 160);
    lv_obj_set_pos(s_fw.lower_slot, 0, 80);
    lv_obj_set_style_bg_opa(s_fw.lower_slot, LV_OPA_TRANSP, 0);
    lv_obj_set_style_border_width(s_fw.lower_slot, 0, 0);
    lv_obj_set_style_pad_all(s_fw.lower_slot, 0, 0);
    lv_obj_set_scrollable(s_fw.lower_slot, false);
    if (!mount_stage(LAB06_STAGE_ROSTER)) return false;
    s_fw.shell_timer = lv_timer_create(timer_tick, LAB06_TIMER_PERIOD_MS, &s_fw);
    if (s_fw.shell_timer == NULL) return false;
    s_fw.initialized = true;
    s_fw.operation_stage_before = LAB06_STAGE_ROSTER;
    update_status(s_fw.active_action, "ready");
    record_resource_sample("boot");
    if (!s_fw.scripted && xTaskCreate(synthetic_worker, "lab06_synthetic", 3072,
                                      NULL, 3, NULL) != pdPASS) return false;
    return true;
}
