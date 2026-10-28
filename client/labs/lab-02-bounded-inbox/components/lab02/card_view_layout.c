#include "lab02/card_view.h"
#include "lab02/layout.h"

#include <stdio.h>

void lab02_card_view_style_root(lv_obj_t *card, bool focused)
{
    lv_obj_set_style_bg_color(card, lv_color_hex(LAB02_UI_CARD), 0);
    lv_obj_set_style_bg_opa(card, LV_OPA_COVER, 0);
    lv_obj_set_style_radius(card, LAB02_CARD_RADIUS, 0);
    lv_obj_set_style_border_width(card, focused ? 2 : 0, 0);
    lv_obj_set_style_border_color(card, lv_color_hex(LAB02_UI_ACCENT), 0);
    lv_obj_set_style_pad_all(card, 8, 0);
}

void lab02_card_view_format_duration(char *buf, size_t cap, uint32_t duration_ms)
{
    uint32_t sec = duration_ms / 1000;
    snprintf(buf, cap, "%u:%02u", (unsigned)(sec / 60), (unsigned)(sec % 60));
}
