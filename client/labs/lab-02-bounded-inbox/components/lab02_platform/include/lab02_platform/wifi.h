#pragma once

#include <stdbool.h>

#ifdef __cplusplus
extern "C" {
#endif

typedef void (*lab02_wifi_transport_fn)(bool reachable);

/** Optional; invoked on STA disconnect (false) and GOT_IP (true). */
void lab02_board_wifi_set_transport_notifier(lab02_wifi_transport_fn fn);

bool lab02_board_wifi_connect(void);

#ifdef __cplusplus
}
#endif
