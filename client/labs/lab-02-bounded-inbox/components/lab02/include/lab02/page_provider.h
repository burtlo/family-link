#pragma once

#include <stddef.h>

#include "lab02/inbox_model.h"
#include "lab02/inbox_page_metadata.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    const lab02_inbox_model_t *model;
    lab02_page_metadata_t meta;
} lab02_page_provider_t;

bool lab02_page_provider_init(lab02_page_provider_t *pp);
bool lab02_page_provider_local_init(lab02_page_provider_t *pp);
const lab02_inbox_item_t *lab02_page_provider_item(const lab02_page_provider_t *pp, int index);

#ifdef __cplusplus
}
#endif
