#pragma once
#include <stdbool.h>
#include <stdint.h>

#define LAB12_QUEUE_CAPACITY 4U
#define LAB12_MAX_CARDS 3U
#define LAB12_COLOR_COUNT 4U
typedef enum { LAB12_SUCCESS, LAB12_FAILURE, LAB12_CANCELLED, LAB12_STALE, LAB12_TIMED_OUT } lab12_terminal_t;
typedef struct {
    uint32_t operation_id;
    lab12_terminal_t terminal;
    bool presented_saved;
    bool active;
    uint8_t terminal_count;
} lab12_operation_t;
typedef struct {
    char saved_accent[8];
    char preview_accent[8];
    char saved_avatar[16];
    char preview_avatar[16];
    char stroke_color[8];
    char inbox_focus[24];
    uint8_t overlay_widgets;
    uint8_t queue_count;
    uint32_t inbox_generation;
    uint8_t update_level;
    bool preview_active;
    bool inbox_root_active;
    lab12_operation_t operation;
} lab12_state_t;
void lab12_init(lab12_state_t *state);
bool lab12_preview(lab12_state_t *state, const char *accent, const char *avatar);
void lab12_cancel_preview(lab12_state_t *state);
bool lab12_open_modal(lab12_state_t *state, const char *card_id);
bool lab12_close_modal(lab12_state_t *state);
bool lab12_cancel_modal(lab12_state_t *state, uint32_t operation_id);
bool lab12_back_to_inbox(lab12_state_t *state);
bool lab12_begin_save(lab12_state_t *state, uint32_t operation_id);
bool lab12_finish_save(lab12_state_t *state, bool accepted, const char *reason);
bool lab12_queue_result(lab12_state_t *state);
bool lab12_set_stroke(lab12_state_t *state, const char *color);
