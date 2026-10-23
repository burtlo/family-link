#ifndef PRODUCT_STAGE_ROUTER_H
#define PRODUCT_STAGE_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif

void product_stage_on_signed_in(void);
int product_stage_on_red_record(void);
int product_stage_on_boot(void);
void product_stage_on_mute(void);
int product_stage_on_wipe_complete(void);
int product_stage_on_capture_failed(void);
int product_stage_on_take_finished(void);

#ifdef __cplusplus
}
#endif

#endif
