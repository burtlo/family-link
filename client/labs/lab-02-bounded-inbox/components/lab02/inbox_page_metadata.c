#include "lab02/inbox_page_metadata.h"

#include <string.h>

void lab02_page_metadata_build(const lab02_inbox_model_t *model, lab02_page_metadata_t *meta)
{
    if (!meta) {
        return;
    }
    memset(meta, 0, sizeof(*meta));
    if (!model || model->count == 0) {
        return;
    }
    meta->total_items = model->count;
    meta->page_size = model->page_size > 0 ? model->page_size : LAB02_DEFAULT_PAGE_SIZE;
    int pages = (model->count + meta->page_size - 1) / meta->page_size;
    if (pages > (int)(sizeof(meta->page_start) / sizeof(meta->page_start[0]))) {
        pages = (int)(sizeof(meta->page_start) / sizeof(meta->page_start[0]));
    }
    meta->page_count = (uint16_t)pages;
    for (int p = 0; p < pages; p++) {
        meta->page_start[p] = (uint16_t)(p * meta->page_size);
    }
}

int lab02_page_metadata_page_for_index(const lab02_page_metadata_t *meta, int item_index)
{
    if (!meta || item_index < 0 || meta->page_size == 0) {
        return -1;
    }
    return item_index / meta->page_size;
}

int lab02_page_metadata_page_item_count(const lab02_page_metadata_t *meta, int page_index)
{
    if (!meta || page_index < 0 || page_index >= (int)meta->page_count) {
        return 0;
    }
    int start = meta->page_start[page_index];
    int end = start + meta->page_size;
    if (end > (int)meta->total_items) {
        end = (int)meta->total_items;
    }
    return end - start;
}
