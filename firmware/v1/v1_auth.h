#ifndef V1_AUTH_H
#define V1_AUTH_H

#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#include "lvgl.h"
#include "v1_types.h"

void v1_auth_init(SemaphoreHandle_t work_sem);
void v1_auth_start_task(void);
void v1_auth_login_task(void *arg);
void v1_auth_set_login_ok_cb(void (*cb)(const char *user_id, bool pin_reset));

bool v1_auth_login_active(void);
uint32_t v1_auth_generation(void);
void v1_auth_set_pick_id(const char *id);
const char *v1_auth_pick_id(void);

size_t v1_auth_entry_len(void);
void v1_auth_clear_entry(void);
void v1_auth_set_entry_len(size_t elen);

void v1_auth_on_roster_pick(const char *user_id);
void v1_auth_on_pin_key(const char *key);
void v1_auth_on_boot_press(void);

void v1_auth_paint_pin(lv_obj_t *scr);
void v1_auth_tick(state_t st);

void v1_auth_on_login_ok(void);

#endif /* V1_AUTH_H */
