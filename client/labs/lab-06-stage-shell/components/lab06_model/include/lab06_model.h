#ifndef LAB06_MODEL_H
#define LAB06_MODEL_H

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define LAB06_EVENT_CAPACITY 8u
#define LAB06_MAX_CONCURRENT_OPERATIONS (LAB06_EVENT_CAPACITY + 1u)
#define LAB06_STAGE_COUNT 6u
#define LAB06_TERMINAL_HISTORY 128u

typedef enum {
    LAB06_STAGE_ROSTER = 0,
    LAB06_STAGE_PIN,
    LAB06_STAGE_HANGOUT,
    LAB06_STAGE_INBOX,
    LAB06_STAGE_RECORD,
    LAB06_STAGE_RECEIPT
} lab06_stage_t;

typedef enum { LAB06_SLOT_UPPER = 0, LAB06_SLOT_LOWER = 1 } lab06_slot_t;
typedef enum { LAB06_TERMINAL_NONE = 0, LAB06_TERMINAL_SUCCESS, LAB06_TERMINAL_FAILURE,
               LAB06_TERMINAL_CANCELLED, LAB06_TERMINAL_STALE } lab06_terminal_t;
typedef enum { LAB06_UPDATE_PROPERTY = 0, LAB06_UPDATE_SUBTREE = 1,
               LAB06_UPDATE_SCENE = 2 } lab06_update_level_t;

typedef struct { int16_t x, y, width, height; } lab06_rect_t;
typedef struct { lab06_rect_t chrome, content; } lab06_slot_geometry_t;

typedef struct {
    uint32_t generation;
    lab06_stage_t stage;
    lab06_slot_t slot;
    uint32_t focused_synthetic_id;
    bool alive;
    char text[48];
    uint32_t updates;
} lab06_panel_t;

typedef struct {
    uint32_t operation_id;
    lab06_terminal_t terminal;
    const char *reason;
    uint8_t terminal_count;
} lab06_operation_result_t;

typedef struct {
    lab06_stage_t stage;
    lab06_stage_t transition_source;
    lab06_stage_t transition_destination;
    uint32_t owner_id;
    uint32_t transition_operation_id;
    uint32_t operation_id_watermark;
    uint32_t next_generation;
    uint32_t timer_count;
    uint32_t terminal_total;
    uint32_t terminal_ids[LAB06_TERMINAL_HISTORY];
    size_t terminal_ids_count;
    size_t terminal_ids_next;
    uint32_t handle_uses;
    uint32_t stale_callbacks;
    uint32_t queue_overflows;
    bool transitioning;
    bool cancel_stage_is_source;
    lab06_panel_t panels[2];
    lab06_operation_result_t last_result;
    uint32_t event_ids[LAB06_EVENT_CAPACITY];
    size_t event_head, event_count;
} lab06_session_t;

lab06_slot_geometry_t lab06_geometry(lab06_slot_t slot);
void lab06_session_init(lab06_session_t *session, uint32_t owner_id, lab06_stage_t initial_stage);
bool lab06_begin_stage_transition(lab06_session_t *session, uint32_t writer_id,
                                  lab06_stage_t destination, uint32_t operation_id);
bool lab06_finish_stage_transition(lab06_session_t *session, uint32_t operation_id);
bool lab06_cancel_stage_transition(lab06_session_t *session, uint32_t operation_id);
bool lab06_apply_panel_data(lab06_session_t *session, lab06_slot_t slot,
                            uint32_t generation, uint32_t operation_id, const char *text,
                            lab06_update_level_t update_level);
bool lab06_panel_handle(const lab06_session_t *session, lab06_slot_t slot,
                        uint32_t *generation_out);
/* Accepted queue operations remain pending until pop records one success terminal.
 * Rejected overflow records one failure terminal; reused/lower ids are ignored. */
bool lab06_event_queue_push(lab06_session_t *session, uint32_t operation_id);
bool lab06_event_queue_pop(lab06_session_t *session, uint32_t *operation_id_out);

#ifdef __cplusplus
}
#endif
#endif
