#ifndef PRODUCT_VOLUME_FW_H
#define PRODUCT_VOLUME_FW_H

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

bool product_volume_attach(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
/* Split-pane teardown: drop pointers to widgets the shell already deleted. */
void product_volume_forget_widgets(void);
bool product_volume_build_widgets(lv_obj_t *upper_slot, lv_obj_t *lower_slot);
void product_volume_service(int64_t now_ms);
int product_volume_active(void);
int product_volume_on_boot_from_inbox(void);
int product_volume_on_boot_leave(void);
int product_volume_on_mute(void);
int product_volume_on_red_record(void);
void product_volume_on_wifi(bool up);
/* Called only by product_stage_present(). */
void product_volume_set_visible(int visible);

#ifdef __cplusplus
}
#include "fl_product_inbox_carousel.hpp"
#include "fl_product_login_session.hpp"
void product_volume_bind_models(fl_product_inbox::CarouselSession *inbox,
                                fl_product_login::SessionWriter *login);
#endif

#endif
