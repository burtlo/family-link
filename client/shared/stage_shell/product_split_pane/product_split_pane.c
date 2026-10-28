#include "product_split_pane.h"

#include "esp_log.h"

#include <string.h>

typedef struct {
    lv_obj_t *screen;
    lv_obj_t *upper_slot;
    lv_obj_t *lower_slot;
    fl_shell_stage_t stage;
    bool initialized;
} product_split_pane_state_t;

static product_split_pane_state_t s_pane;

static bool build_slots(void)
{
    s_pane.upper_slot = lv_obj_create(s_pane.screen);
    s_pane.lower_slot = lv_obj_create(s_pane.screen);
    if (s_pane.upper_slot == NULL || s_pane.lower_slot == NULL) {
        return false;
    }
    lv_obj_set_size(s_pane.upper_slot, 320, 80);
    lv_obj_set_pos(s_pane.upper_slot, 0, 0);
    lv_obj_set_style_border_width(s_pane.upper_slot, 0, 0);
    lv_obj_set_style_pad_all(s_pane.upper_slot, 0, 0);
    lv_obj_set_scrollable(s_pane.upper_slot, false);
    lv_obj_set_style_bg_color(s_pane.upper_slot, lv_color_hex(0x18232B), 0);
    lv_obj_set_style_bg_opa(s_pane.upper_slot, LV_OPA_COVER, 0);

    lv_obj_set_size(s_pane.lower_slot, 320, 160);
    lv_obj_set_pos(s_pane.lower_slot, 0, 80);
    lv_obj_set_style_border_width(s_pane.lower_slot, 0, 0);
    lv_obj_set_style_pad_all(s_pane.lower_slot, 0, 0);
    lv_obj_set_scrollable(s_pane.lower_slot, false);
    lv_obj_set_style_bg_color(s_pane.lower_slot, lv_color_hex(0x202D36), 0);
    lv_obj_set_style_bg_opa(s_pane.lower_slot, LV_OPA_COVER, 0);
    return true;
}

bool product_split_pane_init(lv_obj_t *screen)
{
    if (screen == NULL) {
        return false;
    }
    memset(&s_pane, 0, sizeof(s_pane));
    s_pane.screen = screen;
    s_pane.stage = FL_SHELL_STAGE_ROSTER;
    lv_obj_set_size(screen, 320, 240);
    lv_obj_set_style_bg_color(screen, lv_color_hex(0x101820), 0);
    lv_obj_set_style_pad_all(screen, 0, 0);
    lv_obj_set_style_border_width(screen, 0, 0);
    lv_obj_set_scrollable(screen, false);
    if (!build_slots()) {
        return false;
    }
    s_pane.initialized = true;
    ESP_LOGI("product_split_pane", "V2_PRODUCT_SPLIT_PANE_READY build=%s", FIRMWARE_BUILD_ID);
    return true;
}

bool product_split_pane_slot_roots(lv_obj_t **upper_out, lv_obj_t **lower_out)
{
    if (!s_pane.initialized || upper_out == NULL || lower_out == NULL) {
        return false;
    }
    *upper_out = s_pane.upper_slot;
    *lower_out = s_pane.lower_slot;
    return true;
}

fl_shell_stage_t product_stage_get(void)
{
    return s_pane.initialized ? s_pane.stage : FL_SHELL_STAGE_ROSTER;
}

void product_stage_set(fl_shell_stage_t stage)
{
    if (!s_pane.initialized || (unsigned)stage >= FL_SHELL_STAGE_COUNT) {
        return;
    }
    s_pane.stage = stage;
}

void product_split_pane_release_slots(void)
{
    if (!s_pane.initialized) {
        return;
    }
    if (s_pane.upper_slot != NULL) {
        lv_obj_delete(s_pane.upper_slot);
    }
    if (s_pane.lower_slot != NULL) {
        lv_obj_delete(s_pane.lower_slot);
    }
    s_pane.upper_slot = NULL;
    s_pane.lower_slot = NULL;
}

bool product_split_pane_rebuild_slots(void)
{
    if (!s_pane.initialized) {
        return false;
    }
    if (s_pane.upper_slot != NULL && s_pane.lower_slot != NULL) {
        return true;
    }
    product_split_pane_release_slots();
    return build_slots();
}

bool product_split_pane_slots_live(void)
{
    return s_pane.initialized && s_pane.upper_slot != NULL && s_pane.lower_slot != NULL;
}
