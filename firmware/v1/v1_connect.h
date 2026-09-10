#ifndef V1_CONNECT_H
#define V1_CONNECT_H

#include "v1_types.h"

#include "cJSON.h"
#include "lvgl.h"

void v1_connect_init(void);

bool v1_connect_online(void);
void v1_connect_mark_online(void);
void v1_connect_mark_offline(void);

int v1_connect_user_count(void);
const user_t *v1_connect_users(void);
user_t *v1_connect_users_mut(void);
const char *v1_connect_last_user(void);
const char *v1_connect_user_name(const char *id);
int v1_connect_user_index(const char *id);
uint32_t v1_connect_user_accent(int idx);
uint8_t v1_connect_user_avatar_slot(int idx);
int v1_connect_user_index_by_label(const char *label);

void v1_connect_apply_profile(const char *user_id, cJSON *prof);
void v1_connect_nvs_load_last(char *last_user, size_t cap);
void v1_connect_nvs_save_last(const char *id);
bool v1_connect_nvs_load_hangout(void);

bool v1_connect_load_hangout_ms(int timeout_ms);
bool v1_connect_load_hangout(void);

void v1_connect_enter_from_signin(void);
void v1_connect_signed_out_probe(void);
void v1_connect_tick(int64_t now_us, bool signed_out_pre_auth);

bool v1_connect_signed_out_pre_auth(const char *session_user, state_t st);
bool v1_connect_awaiting_server(const char *session_user, state_t st);

void v1_connect_paint_connecting(lv_obj_t *scr);
void v1_connect_paint_wifi_error(lv_obj_t *scr);
void v1_connect_paint_roster(lv_obj_t *scr);

void v1_connect_refresh_conn_dots(void);
void v1_connect_clear_conn_dots(void);
void v1_connect_refresh_offline_ribbon(const char *session_user);

lv_obj_t *v1_connect_offline_lab(void);
void v1_connect_set_offline_lab(lv_obj_t *lab);

int64_t v1_connect_conn_retry_us(void);
void v1_connect_set_conn_retry_us(int64_t us);
int64_t v1_connect_wifi_retry_us(void);
void v1_connect_set_wifi_retry_us(int64_t us);

#endif /* V1_CONNECT_H */
