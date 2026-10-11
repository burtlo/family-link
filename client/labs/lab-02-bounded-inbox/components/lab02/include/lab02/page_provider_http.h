#pragma once

#include "lab02/carousel_controller.h"
#include "lab02/page_provider.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lab02_page_provider_http_init(lab02_page_provider_t *pp);
void lab02_page_provider_http_start(void);
void lab02_page_provider_http_prefetch_for_focus(int focus_index);
/** Apply pending page merge when snap is idle; returns true if carousel layout should refresh. */
bool lab02_page_provider_http_apply_if_idle(lab02_carousel_t *c);
bool lab02_page_provider_http_active(void);

#ifdef __cplusplus
}
#endif
