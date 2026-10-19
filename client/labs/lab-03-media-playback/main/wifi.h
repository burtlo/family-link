#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lab03_wifi_transport_fn)(bool reachable);

/** Optional; invoked on STA disconnect (false) and GOT_IP (true). */
void lab03_board_wifi_set_transport_notifier(lab03_wifi_transport_fn fn);

bool lab03_board_wifi_connect(void);

#ifdef __cplusplus
}
#endif
