#include "lab12_model.h"
#include <stdio.h>
#include <string.h>

static void copy(char *dst, size_t n, const char *src) { snprintf(dst, n, "%s", src ? src : ""); }
void lab12_init(lab12_state_t *s) {
    memset(s, 0, sizeof(*s)); copy(s->saved_accent, sizeof(s->saved_accent), "#E8C040");
    copy(s->preview_accent, sizeof(s->preview_accent), s->saved_accent);
    copy(s->saved_avatar, sizeof(s->saved_avatar), "avatar-01");
    copy(s->preview_avatar, sizeof(s->preview_avatar), s->saved_avatar);
    copy(s->stroke_color, sizeof(s->stroke_color), "#FFFFFF");
    copy(s->inbox_focus, sizeof(s->inbox_focus), "card-1"); s->inbox_root_active = true;
}
bool lab12_preview(lab12_state_t *s, const char *accent, const char *avatar) {
    if (!accent || strlen(accent) != 7 || accent[0] != '#' || !avatar || strlen(avatar) >= sizeof(s->preview_avatar)) return false;
    for (size_t i=1; i<7; i++) if (!((accent[i]>='0'&&accent[i]<='9')||(accent[i]>='A'&&accent[i]<='F')||(accent[i]>='a'&&accent[i]<='f'))) return false;
    copy(s->preview_accent, sizeof(s->preview_accent), accent); copy(s->preview_avatar, sizeof(s->preview_avatar), avatar);
    s->preview_active = true; s->update_level = 0; return true;
}
void lab12_cancel_preview(lab12_state_t *s) {
    copy(s->preview_accent, sizeof(s->preview_accent), s->saved_accent); copy(s->preview_avatar, sizeof(s->preview_avatar), s->saved_avatar);
    s->preview_active = false; s->update_level = 0;
}
bool lab12_open_modal(lab12_state_t *s, const char *card_id) {
    if (!card_id || !s->inbox_root_active || s->overlay_widgets) return false;
    copy(s->inbox_focus, sizeof(s->inbox_focus), card_id); s->overlay_widgets = 1; s->update_level = 1; return true;
}
bool lab12_close_modal(lab12_state_t *s) { if (!s->overlay_widgets) return false; s->overlay_widgets = 0; s->update_level = 1; return true; }
bool lab12_cancel_modal(lab12_state_t *s,uint32_t id) {
    if(!s||!id||!s->overlay_widgets||s->operation.active)return false;
    s->overlay_widgets=0;s->operation=(lab12_operation_t){.operation_id=id,.terminal=LAB12_CANCELLED,.presented_saved=false,.active=false,.terminal_count=1};s->update_level=1;return true;
}
bool lab12_back_to_inbox(lab12_state_t *s) { if (s->overlay_widgets) return false; s->inbox_root_active = true; s->update_level = 1; return true; }
bool lab12_begin_save(lab12_state_t *s, uint32_t id) {
    if (!id || s->operation.active || s->queue_count >= LAB12_QUEUE_CAPACITY) return false;
    s->operation = (lab12_operation_t){.operation_id=id,.active=true}; s->queue_count++; return true;
}
bool lab12_finish_save(lab12_state_t *s, bool accepted, const char *reason) {
    if (!s->operation.active) return false;
    s->operation.active = false; s->operation.terminal = accepted ? LAB12_SUCCESS : LAB12_FAILURE;
    s->operation.presented_saved = accepted;
    if (accepted) { copy(s->saved_accent,sizeof(s->saved_accent),s->preview_accent); copy(s->saved_avatar,sizeof(s->saved_avatar),s->preview_avatar); }
    s->preview_active = false;
    if (!accepted && reason && strcmp(reason,"rollback")==0) { copy(s->preview_accent,sizeof(s->preview_accent),s->saved_accent); copy(s->preview_avatar,sizeof(s->preview_avatar),s->saved_avatar); }
    s->update_level = 0; if (s->queue_count) s->queue_count--; return true;
}
bool lab12_queue_result(lab12_state_t *s) { if (s->queue_count >= LAB12_QUEUE_CAPACITY) return false; s->queue_count++; return true; }
bool lab12_set_stroke(lab12_state_t *s, const char *color) {
    if (!color || strlen(color) != 7 || color[0] != '#') return false;
    for (size_t i=1; i<7; i++) if (!((color[i]>='0'&&color[i]<='9')||(color[i]>='A'&&color[i]<='F')||(color[i]>='a'&&color[i]<='f'))) return false;
    copy(s->stroke_color,sizeof(s->stroke_color),color); s->update_level = 0; return true;
}
