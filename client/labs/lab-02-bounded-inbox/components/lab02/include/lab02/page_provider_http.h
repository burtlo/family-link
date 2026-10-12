#pragma once

#include "lab02/page_provider.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lab02_page_provider_http_init(lab02_page_provider_t *pp);
void lab02_page_provider_http_start_io(void);
/** Call after the session applies the boot page (LVGL pump may run later than boot_task). */
void lab02_page_provider_http_notify_initial_ready(uint32_t inbox_revision, uint16_t total_count);
bool lab02_page_provider_http_active(void);

#ifdef __cplusplus
}
#endif
