#ifndef FL_SHELL_MODEL_H
#define FL_SHELL_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define FL_SHELL_EVENT_CAPACITY 8u
#define FL_SHELL_MAX_CONCURRENT_OPERATIONS (FL_SHELL_EVENT_CAPACITY + 1u)
#define FL_SHELL_STAGE_COUNT 8u
#define FL_SHELL_TERMINAL_HISTORY 128u

typedef enum {
    FL_SHELL_STAGE_ROSTER = 0,
    FL_SHELL_STAGE_PIN,
    FL_SHELL_STAGE_HANGOUT,
    FL_SHELL_STAGE_INBOX,
    FL_SHELL_STAGE_VOLUME,
    FL_SHELL_STAGE_RECORD,
    FL_SHELL_STAGE_RECEIPT,
    FL_SHELL_STAGE_PICKER
} fl_shell_stage_t;

typedef enum { FL_SHELL_SLOT_UPPER = 0, FL_SHELL_SLOT_LOWER = 1 } fl_shell_slot_t;
typedef enum { FL_SHELL_TERMINAL_NONE = 0, FL_SHELL_TERMINAL_SUCCESS, FL_SHELL_TERMINAL_FAILURE,
               FL_SHELL_TERMINAL_CANCELLED, FL_SHELL_TERMINAL_STALE } fl_shell_terminal_t;
typedef enum { FL_SHELL_UPDATE_PROPERTY = 0, FL_SHELL_UPDATE_SUBTREE = 1,
               FL_SHELL_UPDATE_SCENE = 2 } fl_shell_update_level_t;

typedef struct { int16_t x, y, width, height; } fl_shell_rect_t;
typedef struct { fl_shell_rect_t chrome, content; } fl_shell_slot_geometry_t;

typedef struct {
    uint32_t generation;
    fl_shell_stage_t stage;
    fl_shell_slot_t slot;
    uint32_t focused_synthetic_id;
    bool alive;
    char text[48];
    uint32_t updates;
} fl_shell_panel_t;

typedef struct {
    uint32_t operation_id;
    fl_shell_terminal_t terminal;
    const char *reason;
    uint8_t terminal_count;
} fl_shell_operation_result_t;

typedef struct {
    fl_shell_stage_t stage;
    fl_shell_stage_t transition_source;
    fl_shell_stage_t transition_destination;
    uint32_t owner_id;
    uint32_t transition_operation_id;
    uint32_t operation_id_watermark;
    uint32_t next_generation;
    uint32_t timer_count;
    uint32_t terminal_total;
    uint32_t terminal_ids[FL_SHELL_TERMINAL_HISTORY];
    size_t terminal_ids_count;
    size_t terminal_ids_next;
    uint32_t handle_uses;
    uint32_t stale_callbacks;
    uint32_t queue_overflows;
    bool transitioning;
    bool cancel_stage_is_source;
    fl_shell_panel_t panels[2];
    fl_shell_operation_result_t last_result;
    uint32_t event_ids[FL_SHELL_EVENT_CAPACITY];
    size_t event_head, event_count;
} fl_shell_session_t;

fl_shell_slot_geometry_t fl_shell_geometry(fl_shell_slot_t slot);
void fl_shell_session_init(fl_shell_session_t *session, uint32_t owner_id, fl_shell_stage_t initial_stage);
bool fl_shell_begin_stage_transition(fl_shell_session_t *session, uint32_t writer_id,
                                  fl_shell_stage_t destination, uint32_t operation_id);
bool fl_shell_finish_stage_transition(fl_shell_session_t *session, uint32_t operation_id);
bool fl_shell_cancel_stage_transition(fl_shell_session_t *session, uint32_t operation_id);
bool fl_shell_apply_panel_data(fl_shell_session_t *session, fl_shell_slot_t slot,
                            uint32_t generation, uint32_t operation_id, const char *text,
                            fl_shell_update_level_t update_level);
bool fl_shell_panel_handle(const fl_shell_session_t *session, fl_shell_slot_t slot,
                        uint32_t *generation_out);
/* Accepted queue operations remain pending until pop records one success terminal.
 * Rejected overflow records one failure terminal; reused/lower ids are ignored. */
bool fl_shell_event_queue_push(fl_shell_session_t *session, uint32_t operation_id);
bool fl_shell_event_queue_pop(fl_shell_session_t *session, uint32_t *operation_id_out);

#ifdef __cplusplus
}
#endif
#endif
