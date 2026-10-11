/**
 * Phase B: HTTP paging provider. Phase A ships a stub only.
 */
#include "lab02/page_provider.h"

#include "esp_log.h"

static const char *TAG = "lab02_http";

bool lab02_page_provider_http_init(lab02_page_provider_t *pp)
{
    ESP_LOGW(TAG, "HTTP page provider not implemented (Phase B)");
    (void)pp;
    return false;
}
