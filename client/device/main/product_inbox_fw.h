#ifndef PRODUCT_INBOX_FW_H
#define PRODUCT_INBOX_FW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool product_inbox_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
/* Split-pane teardown: drop pointers to widgets the shell already deleted. */
void product_inbox_forget_widgets(void);
bool product_inbox_build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
/* Presentation tick while signed in: apply results, queue jobs, render. */
void product_inbox_service(int64_t now_ms);
void product_inbox_on_signed_in(void);
void product_inbox_on_mute(void);
/* Free finished inbox HTTP results without applying them. */
void product_inbox_discard_results(void);
void product_inbox_note_refresh(int64_t now_ms);
void product_inbox_note_refresh_failure(int64_t now_ms);
void product_inbox_set_wifi(bool up);
/* Called only by product_stage_present(). */
void product_inbox_set_ui_visible(int visible);
int product_inbox_on_boot_pressed(void);

#ifdef __cplusplus
}
#include "fl_product_inbox_carousel.hpp"
fl_product_inbox::CarouselSession *product_inbox_session(void);
#endif

#endif
