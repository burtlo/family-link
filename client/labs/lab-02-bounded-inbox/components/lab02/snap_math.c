#include "lab02/snap_math.h"

static int32_t stride(void)
{
    return LAB02_CARD_W + LAB02_CARD_GAP;
}

static int32_t content_width(int item_count)
{
    if (item_count <= 0) {
        return 0;
    }
    return LAB02_CARD_GAP + item_count * stride();
}

int32_t lab02_snap_target_x_for_index(int index, int item_count)
{
    if (item_count <= 0 || index < 0) {
        return 0;
    }
    if (index >= item_count) {
        index = item_count - 1;
    }
    int32_t card_center = LAB02_CARD_GAP + index * stride() + LAB02_CARD_W / 2;
    int32_t view_w = LAB02_LCD_W;
    int32_t target = card_center - view_w / 2;
    int32_t max_x = content_width(item_count) - view_w;
    if (max_x < 0) {
        max_x = 0;
    }
    if (target < 0) {
        target = 0;
    }
    if (target > max_x) {
        target = max_x;
    }
    return target;
}

int lab02_snap_index_from_scroll_x(int32_t scroll_x, int item_count)
{
    if (item_count <= 0) {
        return 0;
    }
    int32_t mid = scroll_x + LAB02_LCD_W / 2;
    int32_t rel = mid - LAB02_CARD_GAP - LAB02_CARD_W / 2;
    int idx = (int)(rel / stride());
    if (idx < 0) {
        idx = 0;
    }
    if (idx >= item_count) {
        idx = item_count - 1;
    }
    return idx;
}

int lab02_window_first_index(int32_t scroll_x, int item_count)
{
    if (item_count <= 0) return 0;
    /* Half-open intersections: a card ending exactly at the left edge is out. */
    int32_t rel = scroll_x - LAB02_CARD_GAP - LAB02_CARD_W;
    int first = rel < 0 ? 0 : (int)(rel / stride()) + 1;
    if (first >= item_count) first = item_count - 1;
    return first;
}

int lab02_window_last_index(int32_t scroll_x, int item_count)
{
    if (item_count <= 0) return -1;
    int32_t rel = scroll_x + LAB02_LCD_W - 1 - LAB02_CARD_GAP;
    int last = rel < 0 ? -1 : (int)(rel / stride());
    if (last >= item_count) last = item_count - 1;
    return last;
}
