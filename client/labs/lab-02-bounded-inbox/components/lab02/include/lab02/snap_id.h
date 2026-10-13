#pragma once

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Thread-safe current snap sequence for worker telemetry. */
uint32_t lab02_snap_current_id(void);

#ifdef __cplusplus
}
#endif
