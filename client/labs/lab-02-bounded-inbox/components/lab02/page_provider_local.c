#include "lab02/page_provider.h"

#include "lab02_fixtures/fixtures.h"

#include <string.h>

static lab02_inbox_model_t s_model;
static bool s_loaded;

bool lab02_page_provider_local_init(lab02_page_provider_t *pp)
{
    if (!pp) {
        return false;
    }
    memset(pp, 0, sizeof(*pp));
    const lab02_fixture_blob_t *blob = lab02_fixture_selected();
    if (!blob || !blob->data || blob->size == 0) {
        return false;
    }
    if (!lab02_inbox_model_parse((const char *)blob->data, blob->size, &s_model)) {
        return false;
    }
    s_loaded = true;
    pp->model = &s_model;
    lab02_page_metadata_build(pp->model, &pp->meta);
    return true;
}

const lab02_inbox_item_t *lab02_page_provider_item(const lab02_page_provider_t *pp, int index)
{
    if (!pp || !pp->model) {
        return NULL;
    }
    return lab02_inbox_model_item(pp->model, index);
}
