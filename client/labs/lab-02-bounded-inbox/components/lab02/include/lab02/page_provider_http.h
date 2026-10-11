#pragma once

#include "lab02/page_provider.h"

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

bool lab02_page_provider_http_init(lab02_page_provider_t *pp);
void lab02_page_provider_http_start_io(void);
bool lab02_page_provider_http_active(void);

#ifdef __cplusplus
}
#endif
