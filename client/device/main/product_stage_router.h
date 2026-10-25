#ifndef PRODUCT_STAGE_ROUTER_H
#define PRODUCT_STAGE_ROUTER_H

#ifdef __cplusplus
extern "C" {
#endif

/* The one user-visible surface owner per tick. See
 * docs/standards/device-presentation-invariants.md. */
typedef enum {
    PRODUCT_SCREEN_LOGIN = 0,
    PRODUCT_SCREEN_INBOX,
    PRODUCT_SCREEN_VOLUME,
    PRODUCT_SCREEN_SEND,
    PRODUCT_SCREEN_RECORD,
    PRODUCT_SCREEN_COUNT
} product_screen_t;

product_screen_t product_stage_screen(void);
void product_stage_present(void);
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
