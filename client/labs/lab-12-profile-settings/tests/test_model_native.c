#include "lab12_model.h"
#include "lab12_profile_sync.h"
#include <assert.h>
#include <string.h>
int main(void)
{
    lab12_state_t s;
    lab12_init(&s);
    assert(lab12_preview(&s, "#5AA0E8", "avatar-02"));
    lab12_cancel_preview(&s);
    assert(!s.preview_active && !strcmp(s.saved_accent, "#E8C040"));
    for (unsigned i = 0; i < 5; i++) {
        assert(lab12_open_modal(&s, "card-2"));
        assert(s.overlay_widgets == 1);
        assert(lab12_close_modal(&s));
        assert(s.overlay_widgets == 0);
    }
    assert(!strcmp(s.inbox_focus, "card-2"));
    assert(lab12_open_modal(&s, "card-3"));
    assert(lab12_cancel_modal(&s, 30));
    assert(s.operation.terminal == LAB12_CANCELLED && s.operation.terminal_count == 1 && s.overlay_widgets == 0 && !s.operation.presented_saved);
    assert(lab12_back_to_inbox(&s) && s.update_level <= 1);
    assert(lab12_set_stroke(&s, "#FFFFFF"));
    assert(lab12_preview(&s, "#5AA0E8", "avatar-02"));
    assert(lab12_begin_save(&s, 7));
    assert(lab12_finish_save(&s, false, "rollback"));
    assert(!s.operation.presented_saved && !strcmp(s.saved_accent, "#E8C040"));
    assert(!strcmp(s.stroke_color, "#FFFFFF"));
    assert(!lab12_finish_save(&s, false, "rollback"));
    assert(!lab12_preview(&s, "#ZZ0000", "avatar-03"));
    for (unsigned i = 0; i < LAB12_QUEUE_CAPACITY; i++)
        assert(lab12_queue_result(&s));
    assert(!lab12_begin_save(&s, 8));
    assert(s.queue_count == LAB12_QUEUE_CAPACITY);
    lab12_profile_t initial = {.user_id = "user-a", .revision = 7, .avatar_id = "avatar-01", .profile_accent = "#E8C040"};
    lab12_profile_sync_t sync;
    lab12_profile_sync_init(&sync, &initial, 3);
    lab12_profile_t preview = initial;
    strcpy(preview.profile_accent, "#5AA0E8");
    assert(lab12_profile_sync_preview(&sync, &preview));
    assert(lab12_profile_sync_cancel_preview(&sync));
    assert(!sync.preview_dirty && !strcmp(sync.preview.profile_accent, initial.profile_accent));
    assert(lab12_profile_sync_preview(&sync, &preview));
    assert(lab12_profile_sync_begin(&sync, 41, false, true));
    assert(!lab12_profile_sync_cancel_preview(&sync));
    lab12_profile_t newer_preview = sync.preview;
    strcpy(newer_preview.avatar_id, "avatar-02");
    assert(lab12_profile_sync_preview(&sync, &newer_preview));
    lab12_profile_t ack = initial;
    ack.revision = 8;
    strcpy(ack.profile_accent, "#5AA0E8");
    assert(lab12_profile_sync_complete(&sync, 3, 41, true, &ack));
    assert(sync.acknowledged.revision == 8 && !strcmp(sync.acknowledged.profile_accent, "#5AA0E8"));
    assert(!strcmp(sync.preview.avatar_id, "avatar-02"));
    assert(lab12_profile_sync_begin(&sync, 42, true, false));
    assert(lab12_profile_sync_transport_uncertain(&sync, 3, 42));
    assert(!lab12_profile_sync_begin(&sync, 44, true, false));
    lab12_profile_t unchanged = ack;
    assert(lab12_profile_sync_reconcile(&sync, 3, &unchanged));
    assert(sync.status == LAB12_SYNC_UNCERTAIN);
    lab12_profile_t committed = ack;
    committed.revision = 9;
    strcpy(committed.avatar_id, "avatar-02");
    assert(lab12_profile_sync_reconcile(&sync, 3, &committed));
    assert(sync.acknowledged.revision == 9 && sync.status == LAB12_SYNC_STALE);
    lab12_profile_t old = ack;
    assert(!lab12_profile_sync_reconcile(&sync, 3, &old));
    lab12_profile_t other = {.user_id = "user-b", .revision = 12, .avatar_id = "avatar-03", .profile_accent = "#67C587"};
    assert(!lab12_profile_sync_reconcile(&sync, 3, &other));
    assert(lab12_profile_sync_begin(&sync, 43, true, true));
    lab12_profile_sync_switch_user(&sync, &other, 4);
    assert(!lab12_profile_sync_complete(&sync, 3, 43, true, &committed));
    assert(!strcmp(sync.acknowledged.user_id, "user-b"));
    return 0;
}
