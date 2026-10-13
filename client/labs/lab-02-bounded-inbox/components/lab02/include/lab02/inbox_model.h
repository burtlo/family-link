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

typedef struct {
    uint32_t revision;
    uint16_t total_count;
    uint16_t item_count;
    int page_offset;
    uint32_t snap_id;
    uint64_t server_completed_ns;
    char page_receipt_id[33];
    char run_id[33];
    lab02_inbox_item_t items[LAB02_DEFAULT_PAGE_SIZE];
} lab02_inbox_page_t;

bool lab02_inbox_model_parse(const char *json, size_t len, lab02_inbox_model_t *out);
/** Merge one fl.inbox-page/2 response at page_offset (lab cursor). Updates count to total_count. */
bool lab02_inbox_model_merge_v2_page(lab02_inbox_model_t *model, int page_offset, const char *json,
                                     size_t len, uint32_t *inbox_revision_out);
bool lab02_inbox_model_parse_v2_page(const char *json, size_t len, int page_offset,
                                    lab02_inbox_page_t *out);
bool lab02_inbox_model_apply_v2_page(lab02_inbox_model_t *model,
                                    const lab02_inbox_page_t *page);
const lab02_inbox_item_t *lab02_inbox_model_item(const lab02_inbox_model_t *m, int index);

#ifdef __cplusplus
}
#endif
