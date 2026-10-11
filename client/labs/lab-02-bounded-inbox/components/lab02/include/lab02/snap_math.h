#pragma once

#include "lab02/layout.h"

#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

/** Center-focused horizontal snap target (pixels), clamped to scroll range. */
int32_t lab02_snap_target_x_for_index(int index, int item_count);

/** Nearest item index from current scroll offset. */
int lab02_snap_index_from_scroll_x(int32_t scroll_x, int item_count);

#ifdef __cplusplus
}
#endif
