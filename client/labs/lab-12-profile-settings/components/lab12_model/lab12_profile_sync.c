#include "lab12_profile_sync.h"
#include <string.h>

static bool text_ok(const char *s, unsigned n) { unsigned i=0; if(!s||!s[0]) return false; while(i<n&&s[i]) i++; return i<n; }
void lab12_profile_sync_init(lab12_profile_sync_t *s,const lab12_profile_t *p,uint32_t gen) {
    memset(s,0,sizeof(*s)); if(p) s->acknowledged=*p; s->preview=s->acknowledged; s->session_generation=gen; s->status=LAB12_SYNC_IDLE;
}
bool lab12_profile_sync_preview(lab12_profile_sync_t *s,const lab12_profile_t *p) {
    if(!s||!p||strcmp(p->user_id,s->acknowledged.user_id)!=0||!text_ok(p->avatar_id,sizeof(p->avatar_id))||!text_ok(p->profile_accent,sizeof(p->profile_accent))) return false;
    s->preview=*p; s->preview.revision=s->acknowledged.revision; s->preview_generation++; s->preview_dirty=true; return true;
}
bool lab12_profile_sync_cancel_preview(lab12_profile_sync_t *s) {
    if(!s||s->in_flight.valid)return false;
    s->preview=s->acknowledged;s->preview_dirty=false;s->status=LAB12_SYNC_IDLE;return true;
}
bool lab12_profile_sync_begin(lab12_profile_sync_t *s,uint32_t id,bool avatar,bool accent) {
    if(!s||!id||s->in_flight.valid||(!avatar&&!accent)||s->status==LAB12_SYNC_UNCERTAIN) return false;
    lab12_profile_update_t op={0};
    memcpy(op.user_id,s->acknowledged.user_id,sizeof(op.user_id)); op.session_generation=s->session_generation; op.operation_id=id;
    op.expected_revision=s->acknowledged.revision; op.change_avatar=avatar; op.change_accent=accent; op.preview_generation=s->preview_generation;
    memcpy(op.avatar_id,s->preview.avatar_id,sizeof(op.avatar_id)); memcpy(op.profile_accent,s->preview.profile_accent,sizeof(op.profile_accent)); op.valid=true;
    s->in_flight=op; s->status=LAB12_SYNC_PENDING; return true;
}
static bool same_op(const lab12_profile_sync_t *s,uint32_t gen,uint32_t id) { return s&&s->in_flight.valid&&s->session_generation==gen&&s->in_flight.session_generation==gen&&s->in_flight.operation_id==id&&strcmp(s->in_flight.user_id,s->acknowledged.user_id)==0; }
bool lab12_profile_sync_complete(lab12_profile_sync_t *s,uint32_t gen,uint32_t id,bool accepted,const lab12_profile_t *p) {
    if(!same_op(s,gen,id)) return false;
    if(!accepted) { s->status=LAB12_SYNC_REJECTED; s->in_flight.valid=false; if(s->preview_generation==s->in_flight.preview_generation) { s->preview=s->acknowledged; s->preview_dirty=false; } else s->preview.revision=s->acknowledged.revision; return true; }
    if(!p||strcmp(p->user_id,s->in_flight.user_id)!=0||p->revision<=s->in_flight.expected_revision||s->acknowledged.revision!=s->in_flight.expected_revision) {
        s->status=LAB12_SYNC_STALE; s->in_flight.valid=false; if(!s->preview_dirty) s->preview=s->acknowledged; else s->preview.revision=s->acknowledged.revision; return true;
    }
    if((s->in_flight.change_avatar&&strcmp(p->avatar_id,s->in_flight.avatar_id)!=0)||(s->in_flight.change_accent&&strcmp(p->profile_accent,s->in_flight.profile_accent)!=0)) {
        s->status=LAB12_SYNC_STALE; s->in_flight.valid=false; if(!s->preview_dirty) s->preview=s->acknowledged; else s->preview.revision=s->acknowledged.revision; return true;
    }
    s->acknowledged=*p; if(s->preview_generation==s->in_flight.preview_generation) { s->preview=*p; s->preview_dirty=false; } else s->preview.revision=p->revision;
    s->status=LAB12_SYNC_ACKNOWLEDGED; s->in_flight.valid=false; return true;
}
bool lab12_profile_sync_transport_uncertain(lab12_profile_sync_t *s,uint32_t gen,uint32_t id) {
    if(!same_op(s,gen,id)) return false;
    s->status=LAB12_SYNC_UNCERTAIN;
    return true;
}
bool lab12_profile_sync_reconcile(lab12_profile_sync_t *s,uint32_t gen,const lab12_profile_t *p) {
    if(!s||!p||gen!=s->session_generation||strcmp(p->user_id,s->acknowledged.user_id)!=0) return false;
    if(p->revision<s->acknowledged.revision) return false;
    if(s->in_flight.valid&&p->revision<s->in_flight.expected_revision) return false;
    if(p->revision==s->acknowledged.revision) return true;
    s->acknowledged=*p;
    if(s->in_flight.valid && p->revision>s->in_flight.expected_revision) { s->in_flight.valid=false; s->status=LAB12_SYNC_STALE; }
    else s->status=LAB12_SYNC_ACKNOWLEDGED;
    if(!s->preview_dirty) s->preview=*p; else s->preview.revision=p->revision;
    return true;
}
void lab12_profile_sync_switch_user(lab12_profile_sync_t *s,const lab12_profile_t *p,uint32_t gen) {
    if(!s||!p) return;
    s->acknowledged=*p; s->preview=*p; s->session_generation=gen; s->preview_generation=0; s->preview_dirty=false; memset(&s->in_flight,0,sizeof(s->in_flight)); s->status=LAB12_SYNC_IDLE;
}
