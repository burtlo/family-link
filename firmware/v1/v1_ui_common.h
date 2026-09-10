#ifndef V1_UI_COMMON_H
#define V1_UI_COMMON_H

#include "v1_types.h"

#include "lvgl.h"

void v1_ui_init(void);
void v1_ui_set_activity_cb(void (*cb)(void));

void v1_ui_request_repaint(void);
bool v1_ui_transport_dirty(void);
void v1_ui_clear_transport_dirty(void);
bool v1_ui_repaint_pending(void);
void v1_ui_clear_repaint(void);
void v1_ui_request_transport_refresh(void);

void v1_ui_note_activity(void);
void v1_ui_bump_activity(void);
void v1_ui_hook_scr(lv_obj_t *scr);

void v1_ui_set_status(lv_obj_t *status, const char *t, uint32_t color);
void v1_ui_bind_status(lv_obj_t *status);
void v1_ui_bind_dots(lv_obj_t *dots);
void v1_ui_refresh_dots(size_t elen);

void v1_ui_set_toast(lv_obj_t *toast, const char *msg);
void v1_ui_bind_toast(lv_obj_t *toast);

void v1_ui_request_chirp(int hz);
void v1_ui_request_chirp_pair(int a, int b);
bool v1_ui_take_chirp(int *hz, int *hz2);

void v1_ui_paint_geometry_face(lv_obj_t *parent, uint32_t accent, int sz);
void v1_ui_paint_user_portrait(lv_obj_t *parent, int idx, int sz);
void v1_ui_paint_user_portrait_aligned(lv_obj_t *parent, int idx, int sz,
                                       lv_align_t align, int x_ofs, int y_ofs);
void v1_ui_paint_face(lv_obj_t *parent, int idx, int x, int y);
void v1_ui_paint_face_sized(lv_obj_t *parent, int idx, int x, int y, int sz);
void v1_ui_paint_asterisk_icon(lv_obj_t *parent, int x, int y);

void v1_ui_style_list_row(lv_obj_t *btn);
void v1_ui_style_list_row_label(lv_obj_t *lab);
void v1_ui_roster_row_layout(int n, int *y0, int *row_h, int *row_step, int *face_sz);

void v1_ui_paint_ribbons(lv_obj_t *scr, bool server_online, const char *session_user,
                         lv_obj_t **ribbon_top, lv_obj_t **ribbon_bot,
                         lv_obj_t **count_lab, lv_obj_t **offline_lab, lv_obj_t **toast);

lv_obj_t *v1_ui_paint_icon_box_at(lv_obj_t *scr, int x, int y, int scale_pct);
lv_obj_t *v1_ui_paint_transparent_bar(lv_obj_t *scr, int y, int h);
void v1_ui_paint_mailbox_icon(lv_obj_t *parent, int scale_pct);
void v1_ui_style_conn_title_font(lv_obj_t *lab);
lv_obj_t *v1_ui_paint_message_panel(lv_obj_t *scr, int y, const char *headline,
                                    const char *sub, const char *hint);

typedef struct {
    char *session_user;
    int *vol_notch;
    void (*apply_volume)(int vol);
    int (*roomvol_codec)(int notch);
    uint8_t *card_grad;
    void (*nvs_save_card_grad)(void);
    void (*stop_playback)(void);
    msg_t *msgs;
    int msg_n;
} v1_ui_settings_cfg_t;

typedef struct {
    char *session_user;
    msg_t *msgs;
    int msg_n;
    bool (*session_signed_in)(void);
    void (*stop_playback)(void);
} v1_ui_sleep_cfg_t;

void v1_ui_sleep_init(int64_t activity_us);
void v1_ui_sleep_note_activity(void);
void v1_ui_sleep_bump_idle(void);
bool v1_ui_sleep_is_asleep(void);
bool v1_ui_sleep_is_dimmed(void);
int64_t v1_ui_sleep_activity_us(void);
void v1_ui_sleep_set_dimmed(bool dimmed);
void v1_ui_sleep_enter(void);
void v1_ui_sleep_wake_from_asleep(bool request_full_repaint);

bool v1_ui_can_enter_sleep(state_t st);
void v1_ui_paint_settings(lv_obj_t *scr, const v1_ui_settings_cfg_t *cfg);
void v1_ui_paint_sleep(lv_obj_t *scr, const v1_ui_sleep_cfg_t *cfg);
void v1_ui_refresh_sleep_anim(const v1_ui_sleep_cfg_t *cfg);
bool v1_ui_sleep_tick(int64_t now_us, state_t st, const v1_ui_sleep_cfg_t *cfg);

#endif /* V1_UI_COMMON_H */
