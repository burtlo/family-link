#pragma once

#include <stdint.h>

#include "lab02/inbox_model.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint16_t page_size;
    uint16_t page_count;
    uint16_t total_items;
    /** First item index per page (page_count entries). */
    uint16_t page_start[LAB02_INBOX_MAX_ITEMS / LAB02_DEFAULT_PAGE_SIZE + 1];
} lab02_page_metadata_t;

void lab02_page_metadata_build(const lab02_inbox_model_t *model, lab02_page_metadata_t *meta);
int lab02_page_metadata_page_for_index(const lab02_page_metadata_t *meta, int item_index);
int lab02_page_metadata_page_item_count(const lab02_page_metadata_t *meta, int page_index);

#ifdef __cplusplus
}
#endif
