#include "lab12_ui.h"
#include "lab12_model.h"
#include "lab12_profile_sync.h"
#include "lab12_fixture_client.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/queue.h"
#include "freertos/task.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include "cJSON.h"

extern const unsigned char layout_json_start[] asm("_binary_layout_json_start");

#define BG 0x101418
#define PANEL 0x242C34
#define TEXT 0xE8F0E8
#define MUTED 0xA8B0B8

static lab12_state_t state;
static lab12_profile_sync_t profile_sync;
static QueueHandle_t profile_jobs, profile_results;
static TaskHandle_t profile_worker;
static lv_obj_t *screen, *modal, *title_label, *status_label, *modal_guidance, *settings_root, *inbox_view, *back_button;
static lv_obj_t *accent_button, *avatar_button, *stroke_button, *save_button, *cancel_button, *profile_preview, *profile_preview_label;
static bool phase_shared, mode_scripted;
static bool profile_ready;
static uint32_t scenario_id;
static uint32_t next_ui_operation_id=1000;
typedef enum { LAB12_MODAL_NONE, LAB12_MODAL_CARD, LAB12_MODAL_PROFILE, LAB12_MODAL_STROKE } lab12_modal_kind_t;
static lab12_modal_kind_t modal_kind;

typedef enum { PROFILE_RESULT_ACKNOWLEDGED, PROFILE_RESULT_REJECTED, PROFILE_RESULT_CONFLICT, PROFILE_RESULT_UNCERTAIN } profile_result_kind_t;
typedef struct { lab12_profile_update_t update; uint32_t session_generation; } profile_job_t;
typedef struct {
    lab12_profile_update_t update;
    uint32_t session_generation;
    profile_result_kind_t kind;
    int http_status;
    bool has_profile;
    lab12_profile_t profile;
} profile_result_t;
static void set_status(const char *s);
static void update_profile_control_labels(void);

static const char *accent_values[] = {"#E8C040", "#5AA0E8", "#67C587", "#D875A8"};
static const char *accent_names[] = {"Gold", "Blue", "Green", "Pink"};
static const char *avatar_values[] = {"avatar-01", "avatar-02"};
static const char *stroke_values[] = {"#FFFFFF", "#E85B5B", "#5AA0E8"};
static const char *stroke_names[] = {"White", "Red", "Blue"};
static unsigned accent_index, avatar_index, stroke_index;

static bool profile_is_canonical(const lab12_profile_t *profile,const lab12_profile_update_t *update) {
    return profile&&profile->user_id[0]&&profile->revision>update->expected_revision&&
        profile->avatar_id[0]&&profile->profile_accent[0];
}
static profile_result_t patch_profile(const profile_job_t *job) {
    profile_result_t result={.update=job->update,.session_generation=job->session_generation,.kind=PROFILE_RESULT_UNCERTAIN};
    lab12_profile_t canonical={0};
    int status=lab12_fixture_patch("endpoint-a",&job->update,&canonical,NULL,false,false);
    result.http_status=status;
    if(status==200&&profile_is_canonical(&canonical,&job->update)) {
        result.kind=PROFILE_RESULT_ACKNOWLEDGED; result.profile=canonical; result.has_profile=true; return result;
    }
    if(status==403||status==422) { result.kind=PROFILE_RESULT_REJECTED; return result; }
    if(status>=400&&status<500&&status!=409) { result.kind=PROFILE_RESULT_REJECTED; return result; }

    /* A transport error or malformed success is uncertain. Replay the same immutable
       operation ID/body once; the fixture's idempotency contract resolves a lost reply. */
    if(status<0||status==200) {
        memset(&canonical,0,sizeof(canonical));
        status=lab12_fixture_patch("endpoint-a",&job->update,&canonical,NULL,false,false);
        result.http_status=status;
        if(status==200&&profile_is_canonical(&canonical,&job->update)) {
            result.kind=PROFILE_RESULT_ACKNOWLEDGED; result.profile=canonical; result.has_profile=true; return result;
        }
        if(status==403||status==422) { result.kind=PROFILE_RESULT_REJECTED; return result; }
        if(status>=400&&status<500&&status!=409) { result.kind=PROFILE_RESULT_REJECTED; return result; }
    }

    /* A conflict or repeated transport failure is reconciled from fixture state. */
    memset(&canonical,0,sizeof(canonical));
    if(lab12_fixture_get_profile("endpoint-a",&canonical)) {
        result.profile=canonical; result.has_profile=true;
        if(canonical.revision>job->update.expected_revision)
            result.kind=PROFILE_RESULT_CONFLICT;
    }
    return result;
}
static void profile_io_worker(void *arg) {
    (void)arg;
    profile_job_t job;
    for(;;) {
        if(xQueueReceive(profile_jobs,&job,portMAX_DELAY)!=pdTRUE) continue;
        profile_result_t result=patch_profile(&job);
        (void)xQueueSend(profile_results,&result,portMAX_DELAY);
    }
}
static void copy_profile_to_view(void) {
    snprintf(state.saved_accent,sizeof(state.saved_accent),"%s",profile_sync.acknowledged.profile_accent);
    snprintf(state.saved_avatar,sizeof(state.saved_avatar),"%s",profile_sync.acknowledged.avatar_id);
    snprintf(state.preview_accent,sizeof(state.preview_accent),"%s",profile_sync.preview.profile_accent);
    snprintf(state.preview_avatar,sizeof(state.preview_avatar),"%s",profile_sync.preview.avatar_id);
    state.preview_active=profile_sync.preview_dirty;
}
static void set_profile_busy(bool busy,bool uncertain) {
    if(save_button) {
        lv_label_set_text(lv_obj_get_child(save_button,0),uncertain?"Check status":"Save");
        if(busy&&!uncertain) lv_obj_add_state(save_button,LV_STATE_DISABLED);
        else lv_obj_remove_state(save_button,LV_STATE_DISABLED);
    }
    if(cancel_button) {
        if(busy) lv_obj_add_state(cancel_button,LV_STATE_DISABLED);
        else lv_obj_remove_state(cancel_button,LV_STATE_DISABLED);
    }
    if(back_button) {
        if(busy) lv_obj_add_state(back_button,LV_STATE_DISABLED);
        else lv_obj_remove_state(back_button,LV_STATE_DISABLED);
    }
}
static void apply_profile_result(const profile_result_t *result) {
    if(!result||result->session_generation!=profile_sync.session_generation||
       !profile_sync.in_flight.valid||result->update.operation_id!=profile_sync.in_flight.operation_id||
       strcmp(result->update.user_id,profile_sync.in_flight.user_id)!=0) return;
    if(result->kind==PROFILE_RESULT_UNCERTAIN) {
        (void)lab12_profile_sync_transport_uncertain(&profile_sync,result->session_generation,result->update.operation_id);
        set_profile_busy(false,true);
        set_status("Save outcome is uncertain. Nothing is shown as saved; Check status retries the same operation.");
        return;
    }
    if(result->kind==PROFILE_RESULT_ACKNOWLEDGED&&result->has_profile) {
        (void)lab12_profile_sync_complete(&profile_sync,result->session_generation,result->update.operation_id,true,&result->profile);
    } else {
        (void)lab12_profile_sync_complete(&profile_sync,result->session_generation,result->update.operation_id,false,NULL);
        if(result->kind==PROFILE_RESULT_CONFLICT&&result->has_profile)
            (void)lab12_profile_sync_reconcile(&profile_sync,result->session_generation,&result->profile);
    }
    bool acknowledged=result->kind==PROFILE_RESULT_ACKNOWLEDGED&&profile_sync.status==LAB12_SYNC_ACKNOWLEDGED;
    if(state.operation.active&&state.operation.operation_id==result->update.operation_id)
        (void)lab12_finish_save(&state,acknowledged,acknowledged?NULL:(result->kind==PROFILE_RESULT_CONFLICT?"retain":"rollback"));
    copy_profile_to_view(); update_profile_control_labels(); set_profile_busy(false,false);
    if(acknowledged) {
        if(profile_sync.preview_dirty) set_status("Saved acknowledgement received. Your newer preview remains local and unsaved.");
        else { char message[64]; snprintf(message,sizeof(message),"Saved profile revision %lu.",(unsigned long)profile_sync.acknowledged.revision); set_status(message); }
    } else if(result->kind==PROFILE_RESULT_CONFLICT) {
        set_status("Profile changed elsewhere. The latest revision is loaded; this write was not saved.");
    } else {
        set_status("Rejected; nothing was saved.");
    }
}

static void style_label(lv_obj_t *label, uint32_t color, const lv_font_t *font) {
    lv_obj_set_style_text_color(label, lv_color_hex(color), 0); lv_obj_set_style_text_font(label, font, 0);
}
static void clean_modal(void) {
    if (modal) { lv_obj_delete(modal); modal = NULL; }
    modal_guidance = NULL;
    accent_button = avatar_button = stroke_button = save_button = cancel_button = NULL;
    profile_preview = profile_preview_label = NULL;
    modal_kind = LAB12_MODAL_NONE;
    state.overlay_widgets = 0;
}
static void set_status(const char *s) {
    if (status_label) lv_label_set_text(status_label, s);
    if (modal_guidance && modal) lv_label_set_text(modal_guidance, s);
}
static void close_modal_event(lv_event_t *e) {
    (void)e;
    lab12_close_modal(&state);
    clean_modal();
    set_status("Detail closed; focus returned to the originating control.");
}
static void cancel_modal_event(lv_event_t *e) {
    (void)e;
    if(phase_shared&&profile_sync.in_flight.valid) { set_status("Save is still in progress. Wait for its server result; nothing is presented as saved."); return; }
    if(phase_shared&&profile_ready) (void)lab12_profile_sync_cancel_preview(&profile_sync);
    lab12_cancel_preview(&state);
    lab12_cancel_modal(&state,++scenario_id);
    clean_modal();
    set_status("Preview cancelled. Nothing was saved; focus returned to Settings.");
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *data);
static void create_modal(const char *id, lab12_modal_kind_t kind, const char *title_text, const char *guidance);
static void open_detail(lv_event_t *e) {
    const char *id = (const char *)lv_event_get_user_data(e);
    if (!lab12_open_modal(&state, id)) return;
    create_modal(id, LAB12_MODAL_CARD, "Card details", "Local detail preview. Close returns to this card.");
    button(modal,"Close",12,160,128,48,close_modal_event,NULL);
    button(modal,"Cancel",156,160,128,48,cancel_modal_event,NULL);
}
static void show_settings(lv_event_t *e);
static void update_profile_control_labels(void);
static void update_profile_control_labels(void) {
    char label[32];
    accent_index=0; while(accent_index<3U&&strcmp(state.preview_accent,accent_values[accent_index])!=0) accent_index++;
    avatar_index=strcmp(state.preview_avatar,avatar_values[1])==0?1U:0U;
    if (accent_button) {
        snprintf(label,sizeof(label),"Accent: %s",accent_names[accent_index]);
        lv_label_set_text(lv_obj_get_child(accent_button,0),label);
    }
    if (avatar_button) {
        snprintf(label,sizeof(label),"Avatar: %02u",avatar_index+1U);
        lv_label_set_text(lv_obj_get_child(avatar_button,0),label);
    }
    if (profile_preview && profile_preview_label) {
        char preview[48];
        snprintf(preview,sizeof(preview),"Preview: %s / Avatar %02u",accent_names[accent_index],avatar_index+1U);
        lv_label_set_text(profile_preview_label,preview);
        lv_obj_set_style_bg_color(profile_preview,lv_color_hex((uint32_t)strtoul(accent_values[accent_index]+1,NULL,16)),0);
    }
}
static void choose_accent(lv_event_t *e) {
    (void)e;
    accent_index=(accent_index+1U)%4U;
    if (lab12_preview(&state,accent_values[accent_index],state.preview_avatar)) {
        if(phase_shared&&profile_ready) { lab12_profile_t p=profile_sync.preview; snprintf(p.profile_accent,sizeof(p.profile_accent),"%s",state.preview_accent); (void)lab12_profile_sync_preview(&profile_sync,&p); }
        update_profile_control_labels();
        set_status("Accent preview only. Save requires a server acknowledgement.");
    }
}
static void choose_avatar(lv_event_t *e) {
    (void)e;
    avatar_index=(avatar_index+1U)%2U;
    if (lab12_preview(&state,state.preview_accent,avatar_values[avatar_index])) {
        if(phase_shared&&profile_ready) { lab12_profile_t p=profile_sync.preview; snprintf(p.avatar_id,sizeof(p.avatar_id),"%s",state.preview_avatar); (void)lab12_profile_sync_preview(&profile_sync,&p); }
        update_profile_control_labels();
        set_status("Avatar preview only. Accent and sketch stroke remain separate.");
    }
}
static void choose_stroke(lv_event_t *e) {
    (void)e;
    stroke_index=(stroke_index+1U)%3U;
    if (lab12_set_stroke(&state,stroke_values[stroke_index]) && stroke_button) {
        char label[32];
        snprintf(label,sizeof(label),"Stroke: %s",stroke_names[stroke_index]);
        lv_label_set_text(lv_obj_get_child(stroke_button,0),label);
        set_status("Sketch stroke changed locally; profile accent is separate.");
    }
}
static void save_probe(lv_event_t *e) {
    (void)e;
    if (!phase_shared) { set_status("Visual phase has no server. Preview is not saved."); return; }
    if(!profile_ready||!profile_worker) { set_status("Shared profile is unavailable. Preview is retained locally; nothing was saved."); return; }
    if(profile_sync.status==LAB12_SYNC_PENDING) { set_status("Save is in progress. Wait for the server acknowledgement."); return; }
    profile_job_t job={0};
    if(profile_sync.status==LAB12_SYNC_UNCERTAIN&&profile_sync.in_flight.valid) {
        job.update=profile_sync.in_flight; job.session_generation=profile_sync.session_generation;
        if(xQueueSend(profile_jobs,&job,0)!=pdTRUE) { set_status("Could not queue status check. Nothing was marked saved."); return; }
        set_profile_busy(true,false); set_status("Checking the same operation with the fixture; nothing is shown as saved yet."); return;
    }
    lab12_profile_t candidate=profile_sync.preview;
    snprintf(candidate.profile_accent,sizeof(candidate.profile_accent),"%s",state.preview_accent);
    snprintf(candidate.avatar_id,sizeof(candidate.avatar_id),"%s",state.preview_avatar);
    if(!lab12_profile_sync_preview(&profile_sync,&candidate)) { set_status("Preview is invalid. Nothing was saved."); return; }
    bool change_avatar=strcmp(profile_sync.preview.avatar_id,profile_sync.acknowledged.avatar_id)!=0;
    bool change_accent=strcmp(profile_sync.preview.profile_accent,profile_sync.acknowledged.profile_accent)!=0;
    if(!change_avatar&&!change_accent) { set_status("No profile changes to save."); return; }
    uint32_t operation_id=++next_ui_operation_id;
    if(!lab12_begin_save(&state,operation_id)) { set_status("Save queue is full. Nothing was saved."); return; }
    if(!lab12_profile_sync_begin(&profile_sync,operation_id,change_avatar,change_accent)) {
        (void)lab12_finish_save(&state,false,"retain"); set_status("Another profile update is unresolved. Nothing was saved."); return;
    }
    job.update=profile_sync.in_flight; job.session_generation=profile_sync.session_generation;
    if(xQueueSend(profile_jobs,&job,0)!=pdTRUE) {
        (void)lab12_profile_sync_complete(&profile_sync,job.session_generation,operation_id,false,NULL);
        (void)lab12_finish_save(&state,false,"rollback"); copy_profile_to_view(); update_profile_control_labels();
        set_status("Save queue is full. Nothing was saved."); return;
    }
    set_profile_busy(true,false);
    set_status("Saving profile. Wait for the server acknowledgement; preview is not yet saved.");
}
static void return_inbox(lv_event_t *e) {
    (void)e;
    if(phase_shared&&profile_sync.in_flight.valid) { set_status("Save is still unresolved. Wait for its result before leaving Settings."); return; }
    if (modal_kind != LAB12_MODAL_NONE) lab12_cancel_modal(&state,++scenario_id);
    clean_modal();
    lab12_cancel_preview(&state);
    if(phase_shared&&profile_ready) (void)lab12_profile_sync_cancel_preview(&profile_sync);
    lab12_back_to_inbox(&state);
    if (settings_root) lv_obj_set_hidden(settings_root,true);
    if (inbox_view) lv_obj_set_hidden(inbox_view,false);
    if (title_label) lv_label_set_text(title_label,"Profile Settings");
    if (status_label) {
        lv_obj_set_width(status_label,296);
        lv_label_set_text(status_label,"Card details. Settings previews.");
        lv_obj_set_hidden(status_label,false);
    }
    if (back_button) lv_obj_set_hidden(back_button,true);
    set_status("Inbox returned with the same three cards and focus.");
}
static lv_obj_t *button(lv_obj_t *parent,const char *text,int x,int y,int w,int h,lv_event_cb_t cb,void *data) {
    lv_obj_t *b=lv_button_create(parent); lv_obj_set_size(b,w,h); lv_obj_set_pos(b,x,y);
    lv_obj_t *l=lv_label_create(b); lv_label_set_text(l,text); lv_obj_center(l); style_label(l,TEXT,&lv_font_montserrat_16);
    if(cb) lv_obj_add_event_cb(b,cb,LV_EVENT_CLICKED,data);
    return b;
}
static void create_modal(const char *id,lab12_modal_kind_t kind,const char *title_text,const char *guidance) {
    (void)id;
    modal_kind=kind;
    modal=lv_obj_create(screen);
    lv_obj_set_size(modal,296,216); lv_obj_set_pos(modal,12,12);
    lv_obj_set_scrollable(modal,false);
    lv_obj_set_style_bg_color(modal,lv_color_hex(PANEL),0);
    lv_obj_set_style_border_width(modal,1,0);
    lv_obj_set_style_border_color(modal,lv_color_hex(TEXT),0);
    lv_obj_set_style_pad_all(modal,0,0);
    lv_obj_t *title=lv_label_create(modal); lv_label_set_text(title,title_text); lv_obj_set_pos(title,8,6); style_label(title,TEXT,&lv_font_montserrat_24);
    modal_guidance=lv_label_create(modal); lv_label_set_text(modal_guidance,guidance); lv_obj_set_width(modal_guidance,280); lv_obj_set_pos(modal_guidance,8,38); style_label(modal_guidance,MUTED,&lv_font_montserrat_16);
}
static void open_profile_settings(lv_event_t *e) {
    (void)e;
    if (!lab12_open_modal(&state,"settings-profile")) return;
    accent_index=0; while(accent_index<3U&&strcmp(state.preview_accent,accent_values[accent_index])!=0) accent_index++;
    avatar_index=strcmp(state.preview_avatar,avatar_values[1])==0?1U:0U;
    create_modal("settings-profile",LAB12_MODAL_PROFILE,"Profile preview","Accent and avatar preview locally; Save needs acknowledgement.");
    accent_button=button(modal,"Accent",12,64,128,48,choose_accent,NULL);
    avatar_button=button(modal,"Avatar",156,64,128,48,choose_avatar,NULL);
    save_button=button(modal,"Save",12,116,128,48,save_probe,NULL);
    cancel_button=button(modal,"Cancel",156,116,128,48,cancel_modal_event,NULL);
    profile_preview=lv_obj_create(modal); lv_obj_set_size(profile_preview,280,40); lv_obj_set_pos(profile_preview,8,168);
    lv_obj_set_scrollable(profile_preview,false);
    lv_obj_set_style_bg_opa(profile_preview,LV_OPA_COVER,0);
    lv_obj_set_style_radius(profile_preview,8,0); lv_obj_set_style_border_width(profile_preview,1,0); lv_obj_set_style_border_color(profile_preview,lv_color_hex(TEXT),0);
    profile_preview_label=lv_label_create(profile_preview); lv_obj_center(profile_preview_label); style_label(profile_preview_label,BG,&lv_font_montserrat_16);
    update_profile_control_labels();
}
static void open_stroke_settings(lv_event_t *e) {
    (void)e;
    if (!lab12_open_modal(&state,"settings-stroke")) return;
    stroke_index=0; while(stroke_index<2U&&strcmp(state.stroke_color,stroke_values[stroke_index])!=0) stroke_index++;
    create_modal("settings-stroke",LAB12_MODAL_STROKE,"Sketch stroke","This local drawing swatch is separate from profile accent.");
    stroke_button=button(modal,"Stroke",12,64,272,48,choose_stroke,NULL);
    char label[32]; snprintf(label,sizeof(label),"Stroke: %s",stroke_names[stroke_index]);
    lv_label_set_text(lv_obj_get_child(stroke_button,0),label);
    button(modal,"Done",12,116,128,48,close_modal_event,NULL);
    button(modal,"Cancel",156,116,128,48,cancel_modal_event,NULL);
}
static void show_settings(lv_event_t *e) {
    (void)e;
    if (inbox_view) lv_obj_set_hidden(inbox_view,true);
    if (!settings_root) {
        settings_root=lv_obj_create(lv_obj_get_parent(inbox_view));
        lv_obj_set_size(settings_root,320,160); lv_obj_set_pos(settings_root,0,0);
        lv_obj_set_style_bg_opa(settings_root,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(settings_root,0,0);
        lv_obj_set_style_pad_all(settings_root,0,0);
        lv_obj_t *hint=lv_label_create(settings_root); lv_label_set_text(hint,"Preview is local until Save."); lv_obj_set_width(hint,296); lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP); lv_obj_set_pos(hint,12,12); style_label(hint,TEXT,&lv_font_montserrat_16);
        button(settings_root,"Profile accent and avatar",16,36,288,48,open_profile_settings,NULL);
        button(settings_root,"Sketch stroke color",16,88,288,48,open_stroke_settings,NULL);
    }
    lv_obj_set_hidden(settings_root,false);
    if (title_label) lv_label_set_text(title_label,"Settings");
    if (status_label) {
        lv_obj_set_width(status_label,174);
        lv_label_set_text(status_label,"Preview stays local.");
        lv_obj_set_hidden(status_label,false);
    }
    if (back_button) lv_obj_set_hidden(back_button,false);
    set_status("Settings. Open a card to preview; Cancel discards local changes.");
}
static void settings_click(lv_event_t *e) { show_settings(e); }
static void back_inbox_click(lv_event_t *e) { (void)e; return_inbox(NULL); }
static void status_cb(lv_timer_t *timer) {
    (void)timer;
    if(!profile_results) return;
    profile_result_t result;
    while(xQueueReceive(profile_results,&result,0)==pdTRUE) apply_profile_result(&result);
}
bool lab12_ui_init(lv_obj_t *root,bool shared,bool scripted) {
    screen=root; phase_shared=shared; mode_scripted=scripted; profile_ready=!shared; lab12_init(&state);
    if(shared) {
        profile_jobs=xQueueCreate(1,sizeof(profile_job_t)); profile_results=xQueueCreate(1,sizeof(profile_result_t));
        if(!profile_jobs||!profile_results) return false;
    }
    lv_obj_set_style_bg_color(screen,lv_color_hex(BG),0);
    lv_obj_t *upper=lv_obj_create(screen); lv_obj_set_size(upper,320,80); lv_obj_set_pos(upper,0,0); lv_obj_set_style_bg_color(upper,lv_color_hex(BG),0); lv_obj_set_style_border_width(upper,0,0); lv_obj_set_style_pad_all(upper,0,0); lv_obj_set_scrollable(upper,false);
    lv_obj_t *lower=lv_obj_create(screen); lv_obj_set_size(lower,320,160); lv_obj_set_pos(lower,0,80); lv_obj_set_style_bg_color(lower,lv_color_hex(PANEL),0); lv_obj_set_style_border_width(lower,0,0); lv_obj_set_style_pad_all(lower,0,0); lv_obj_set_scrollable(lower,false);
    title_label=lv_label_create(upper); lv_label_set_text(title_label,"Profile Settings"); lv_obj_set_pos(title_label,12,12); style_label(title_label,TEXT,&lv_font_montserrat_24);
    status_label=lv_label_create(upper); lv_label_set_text(status_label,"Card details. Settings previews."); lv_obj_set_width(status_label,296); lv_label_set_long_mode(status_label,LV_LABEL_LONG_DOT); lv_obj_set_pos(status_label,12,45); style_label(status_label,MUTED,&lv_font_montserrat_16);
    inbox_view=lv_obj_create(lower); lv_obj_set_size(inbox_view,320,160); lv_obj_set_pos(inbox_view,0,0); lv_obj_set_scrollable(inbox_view,false);
    lv_obj_set_style_bg_opa(inbox_view,LV_OPA_TRANSP,0); lv_obj_set_style_border_width(inbox_view,0,0); lv_obj_set_style_pad_all(inbox_view,0,0);
    lv_obj_t *hint=lv_label_create(inbox_view); lv_label_set_text(hint,"Tap a card for details."); lv_obj_set_width(hint,296); lv_label_set_long_mode(hint,LV_LABEL_LONG_WRAP); lv_obj_set_pos(hint,12,12); style_label(hint,TEXT,&lv_font_montserrat_16);
    const char *cards[]={"Card 1","Card 2","Card 3"}; const char *ids[]={"card-1","card-2","card-3"};
    for(int i=0;i<3;i++) button(inbox_view,cards[i],16+(i%2)*152,36+(i/2)*52,136,48,open_detail,(void*)ids[i]);
    button(inbox_view,"Settings",168,88,136,48,settings_click,NULL);
    back_button=button(upper,"Back",195,12,101,48,back_inbox_click,NULL);
    lv_obj_set_hidden(back_button,true);
    return lv_timer_create(status_cb,100,NULL)!=NULL;
}
bool lab12_ui_profile_ready(const lab12_profile_t *profile) {
    if(!phase_shared||!profile||!profile->user_id[0]) return false;
    bool had_preview=state.preview_active;
    char preview_accent[sizeof(state.preview_accent)],preview_avatar[sizeof(state.preview_avatar)];
    snprintf(preview_accent,sizeof(preview_accent),"%s",state.preview_accent);
    snprintf(preview_avatar,sizeof(preview_avatar),"%s",state.preview_avatar);
    lab12_profile_sync_init(&profile_sync,profile,1);
    if(had_preview) {
        lab12_profile_t candidate=*profile;
        snprintf(candidate.profile_accent,sizeof(candidate.profile_accent),"%s",preview_accent);
        snprintf(candidate.avatar_id,sizeof(candidate.avatar_id),"%s",preview_avatar);
        if(!lab12_profile_sync_preview(&profile_sync,&candidate)) return false;
    }
    copy_profile_to_view(); profile_ready=true; update_profile_control_labels();
    if(!profile_worker&&xTaskCreate(profile_io_worker,"lab12_profile_io",8192,NULL,4,&profile_worker)!=pdPASS) {
        profile_ready=false; set_status("Profile loaded, but save worker is unavailable. Preview is not saved."); return false;
    }
    set_status("Shared profile loaded. Preview is local until the server acknowledges Save.");
    return true;
}
void lab12_ui_profile_unavailable(void) {
    profile_ready=false;
    set_status("Offline: previews stay local. Nothing is saved until the profile is available.");
}
void lab12_run_harness(void) {
    if (!mode_scripted) return;
    if (!phase_shared) {
        lab12_state_t probe; lab12_init(&probe);
        lab12_preview(&probe,"#5AA0E8",probe.preview_avatar); lab12_cancel_preview(&probe);
        printf("LAB12_TRACE {\"scenario\":\"P-preview\",\"operation_id\":1,\"terminal\":\"cancelled\",\"terminal_count\":1,\"saved\":%u,\"success_copy\":0}\n",probe.preview_active?1U:0U);
        bool opened=lab12_open_modal(&probe,"card-2"); bool closed=lab12_close_modal(&probe);
        printf("LAB12_TRACE {\"scenario\":\"P-modal\",\"operation_id\":2,\"terminal\":\"%s\",\"terminal_count\":1,\"focus_id\":\"%s\",\"overlay_widgets\":%u}\n",opened&&closed?"success":"failure",probe.inbox_focus,(unsigned)probe.overlay_widgets);
        bool returned=lab12_back_to_inbox(&probe);
        printf("LAB12_TRACE {\"scenario\":\"P-return\",\"operation_id\":3,\"terminal\":\"%s\",\"terminal_count\":1,\"inbox_root\":%u,\"focus_id\":\"%s\",\"update_level\":%u}\n",returned?"success":"failure",probe.inbox_root_active?1U:0U,probe.inbox_focus,(unsigned)probe.update_level);
        cJSON *fixture=cJSON_Parse((const char*)layout_json_start);cJSON *layout=fixture?cJSON_GetObjectItemCaseSensitive(fixture,"layout"):NULL,*slots=fixture?cJSON_GetObjectItemCaseSensitive(fixture,"slots"):NULL;
        cJSON *upper=slots?cJSON_GetObjectItemCaseSensitive(slots,"upper"):NULL,*lower=slots?cJSON_GetObjectItemCaseSensitive(slots,"lower"):NULL;
        if(cJSON_IsString(layout)&&cJSON_IsArray(upper)&&cJSON_IsArray(lower))
            printf("LAB12_TRACE {\"scenario\":\"P-layout\",\"class\":\"exploratory\",\"operation_id\":9,\"terminal\":\"observed\",\"terminal_count\":1,\"layout\":\"%s\",\"upper\":[%d,%d],\"lower\":[%d,%d]}\n",layout->valuestring,cJSON_GetArrayItem(upper,0)->valueint,cJSON_GetArrayItem(upper,1)->valueint,cJSON_GetArrayItem(lower,0)->valueint,cJSON_GetArrayItem(lower,1)->valueint);
        if(fixture)cJSON_Delete(fixture);
    } else {
        lab12_profile_t initial={0},peer={0},canonical={0}; lab12_profile_sync_t sync;
        char reject[16]={0},offline[16]={0},layout[32]={0},message_color[64]={0}; uint32_t conflict_revision=0,side_before=0,side_after=0;
        if(!lab12_fixture_get_profile("endpoint-a",&initial)||!lab12_fixture_get_probes(reject,sizeof(reject),offline,sizeof(offline),layout,sizeof(layout),message_color,sizeof(message_color))){printf("LAB12_RUNTIME_FAILED reason=fixture_get\n");return;}
        lab12_profile_sync_init(&sync,&initial,1);
        lab12_profile_t p=sync.preview; snprintf(p.profile_accent,sizeof(p.profile_accent),"#5AA0E8"); lab12_profile_sync_preview(&sync,&p);
        lab12_profile_sync_begin(&sync,4,false,true); lab12_profile_update_t op=sync.in_flight;
        int rejected=lab12_fixture_patch("endpoint-a",&op,NULL,NULL,true,false);
        lab12_profile_sync_complete(&sync,1,4,rejected!=403,NULL);
        bool rejected_as_expected=rejected==403&&sync.status==LAB12_SYNC_REJECTED;
        printf("LAB12_TRACE {\"scenario\":\"P-save-fail\",\"operation_id\":4,\"terminal\":\"%s\",\"terminal_count\":1,\"presented_saved\":%u}\n",rejected_as_expected?"failure":"unexpected",sync.status==LAB12_SYNC_ACKNOWLEDGED?1U:0U);

        p=sync.preview;snprintf(p.profile_accent,sizeof(p.profile_accent),"#5AA0E8");lab12_profile_sync_preview(&sync,&p);lab12_profile_sync_begin(&sync,5,false,true);op=sync.in_flight;
        int first_status=lab12_fixture_patch("endpoint-a",&op,&canonical,NULL,false,false);bool first_ok=first_status==200&&lab12_profile_sync_complete(&sync,1,5,true,&canonical);
        lab12_fixture_get_profile("endpoint-b",&peer); lab12_profile_update_t stale=op;stale.operation_id=55;stale.expected_revision=7;stale.change_avatar=true;stale.change_accent=false;snprintf(stale.avatar_id,sizeof(stale.avatar_id),"avatar-02");
        int conflict=lab12_fixture_patch("endpoint-b",&stale,NULL,&conflict_revision,false,false);lab12_profile_t unchanged={0};lab12_fixture_get_profile("endpoint-a",&unchanged);
        bool conflict_ok=first_ok&&conflict==409&&conflict_revision==8&&unchanged.revision==canonical.revision&&strcmp(unchanged.profile_accent,canonical.profile_accent)==0;
        printf("LAB12_TRACE {\"scenario\":\"P-conflict\",\"operation_id\":5,\"terminal\":\"%s\",\"terminal_count\":1,\"first_success\":%u,\"revision\":%lu,\"first_body_unchanged\":%u}\n",conflict_ok?"revision_conflict":"failure",first_ok?1U:0U,(unsigned long)conflict_revision,(unchanged.revision==canonical.revision&&strcmp(unchanged.profile_accent,canonical.profile_accent)==0)?1U:0U);

        p=sync.preview;snprintf(p.avatar_id,sizeof(p.avatar_id),"avatar-02");lab12_profile_sync_preview(&sync,&p);lab12_profile_sync_begin(&sync,6,true,false);op=sync.in_flight;
        lab12_fixture_get_stats(&side_before);int lost=lab12_fixture_patch("endpoint-b",&op,NULL,NULL,false,true);
        lab12_profile_sync_transport_uncertain(&sync,1,6);int replay=lab12_fixture_patch("endpoint-b",&op,&canonical,NULL,false,true);
        lab12_fixture_get_stats(&side_after);bool replay_ok=lost<0&&replay==200&&side_after-side_before==1&&lab12_profile_sync_complete(&sync,1,6,true,&canonical);
        printf("LAB12_TRACE {\"scenario\":\"P-lost\",\"operation_id\":6,\"terminal\":\"%s\",\"terminal_count\":1,\"replayed_same_operation_id\":1,\"replay_count\":1,\"side_effect_count\":%lu}\n",replay_ok?"success":"failure",(unsigned long)(side_after-side_before));

        lab12_profile_t before_invalid={0},after_invalid={0};
        bool got_before=lab12_fixture_get_profile("endpoint-a",&before_invalid);
        p=sync.preview;snprintf(p.profile_accent,sizeof(p.profile_accent),"#000000");lab12_profile_sync_preview(&sync,&p);lab12_profile_sync_begin(&sync,7,false,true);op=sync.in_flight;
        int invalid=lab12_fixture_patch("endpoint-a",&op,NULL,NULL,false,false);lab12_profile_sync_complete(&sync,1,7,false,NULL);
        bool got_after=lab12_fixture_get_profile("endpoint-a",&after_invalid);
        bool profile_unchanged=got_before&&got_after&&before_invalid.revision==after_invalid.revision&&
            strcmp(before_invalid.user_id,after_invalid.user_id)==0&&strcmp(before_invalid.avatar_id,after_invalid.avatar_id)==0&&
            strcmp(before_invalid.profile_accent,after_invalid.profile_accent)==0;
        bool fallback_tile=strcmp(before_invalid.avatar_id,"avatar-01")==0||strcmp(before_invalid.avatar_id,"avatar-02")==0;
        bool invalid_ok=invalid==422&&profile_unchanged&&fallback_tile;
        printf("LAB12_TRACE {\"scenario\":\"P-invalid\",\"operation_id\":7,\"terminal\":\"%s\",\"terminal_count\":1,\"profile_unchanged\":%u,\"fallback_avatar\":%u}\n",invalid_ok?"invalid":"failure",profile_unchanged?1U:0U,fallback_tile?1U:0U);

        char stroke_before[sizeof(state.stroke_color)];snprintf(stroke_before,sizeof(stroke_before),"%s",state.stroke_color);uint32_t inbox_before=state.inbox_generation;
        p=sync.acknowledged;snprintf(p.profile_accent,sizeof(p.profile_accent),"#67C587");lab12_profile_update_t peer_op={0};snprintf(peer_op.user_id,sizeof(peer_op.user_id),"%s",p.user_id);peer_op.session_generation=1;peer_op.operation_id=8;peer_op.expected_revision=p.revision;peer_op.change_accent=true;snprintf(peer_op.profile_accent,sizeof(peer_op.profile_accent),"#67C587");peer_op.valid=true;
        bool peer_saved=lab12_fixture_patch("endpoint-b",&peer_op,&canonical,NULL,false,false)==200;bool hinted=peer_saved&&lab12_fixture_get_profile("endpoint-a",&canonical)&&lab12_profile_sync_reconcile(&sync,1,&canonical);
        printf("LAB12_TRACE {\"scenario\":\"P-hint\",\"operation_id\":8,\"terminal\":\"%s\",\"terminal_count\":1,\"stroke_color_changed\":%u,\"inbox_rebuilds\":%u}\n",hinted?"success":"failure",strcmp(stroke_before,state.stroke_color)!=0?1U:0U,(unsigned)(state.inbox_generation-inbox_before));
        printf("LAB12_TRACE {\"scenario\":\"P-save-policy\",\"class\":\"exploratory\",\"operation_id\":10,\"terminal\":\"observed\",\"terminal_count\":1,\"reject_preview\":\"%s\"}\n",reject);
        printf("LAB12_TRACE {\"scenario\":\"P-offline\",\"class\":\"exploratory\",\"operation_id\":11,\"terminal\":\"failure\",\"terminal_count\":1,\"presented_saved\":0,\"work\":\"%s\"}\n",offline);
        char *separator=strchr(message_color,','); if(separator){*separator='\0';printf("LAB12_TRACE {\"scenario\":\"P-message-color\",\"class\":\"exploratory\",\"operation_id\":12,\"terminal\":\"observed\",\"terminal_count\":1,\"values\":[\"%s\",\"%s\"]}\n",message_color,separator+1);}
    }
    printf("LAB12_HARNESS_DONE status=complete phase=%s\n",phase_shared?"shared":"visual");
}
