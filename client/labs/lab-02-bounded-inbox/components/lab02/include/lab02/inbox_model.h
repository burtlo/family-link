#pragma once

#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>

#include "lab02/layout.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    char message_id[24];
    char sender_id[16];
    char sender_name[32];
    char preview[64];
    uint32_t duration_ms;
    uint32_t seq;
    bool unread;
} lab02_inbox_item_t;

typedef struct {
    char schema[32];
    char revision[16];
    uint16_t page_size;
    uint16_t count;
    lab02_inbox_item_t items[LAB02_INBOX_MAX_ITEMS];
} lab02_inbox_model_t;

bool lab02_inbox_model_parse(const char *json, size_t len, lab02_inbox_model_t *out);
/** Merge one fl.inbox-page/2 response at page_offset (lab cursor). Updates count to total_count. */
bool lab02_inbox_model_merge_v2_page(lab02_inbox_model_t *model, int page_offset, const char *json,
                                     size_t len, uint32_t *inbox_revision_out);
const lab02_inbox_item_t *lab02_inbox_model_item(const lab02_inbox_model_t *m, int index);

#ifdef __cplusplus
}
#endif
