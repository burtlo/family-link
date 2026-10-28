#include "fl_product_send_picker.hpp"

#include <stdio.h>
#include <string.h>

namespace fl_product_send
{
namespace
{

enum { kHeaderLimit = 25 };

void append_text(char *out, int cap, const char *src)
{
    int used;
    int i;
    if (out == 0 || cap <= 0 || src == 0) {
        return;
    }
    used = (int)strlen(out);
    if (used >= cap - 1) {
        return;
    }
    for (i = 0; src[i] != '\0' && used + 1 < cap; ++i) {
        out[used] = src[i];
        used += 1;
    }
    out[used] = '\0';
}

void copy_text(char *out, int cap, const char *src)
{
    if (out == 0 || cap <= 0) {
        return;
    }
    out[0] = '\0';
    append_text(out, cap, src);
}

void first_token(const char *name, char *out, int cap)
{
    int i = 0;
    int n = 0;
    if (out == 0 || cap <= 0) {
        return;
    }
    out[0] = '\0';
    if (name == 0) {
        return;
    }
    while (name[i] == ' ' || name[i] == '\t') {
        i += 1;
    }
    while (name[i] != '\0' && name[i] != ' ' && name[i] != '\t' && n + 1 < cap) {
        out[n] = name[i];
        n += 1;
        i += 1;
    }
    out[n] = '\0';
}

/* Login's card_bg_recognized list. card-bg-01 through card-bg-06 are the
   start of that set; 07 and 08 are the same palette. */
int card_background_known(const char *id)
{
    static const char *kKnown[] = {"card-bg-01", "card-bg-02", "card-bg-03", "card-bg-04",
                                   "card-bg-05", "card-bg-06", "card-bg-07", "card-bg-08"};
    int i;
    if (id == 0 || id[0] == '\0') {
        return 0;
    }
    for (i = 0; i < 8; ++i) {
        if (strcmp(id, kKnown[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

/* Login stores avatar ids and does not publish a recognized-avatar table.
   These are the provisional catalog ids used for the same initials rule. */
int avatar_known(const char *id)
{
    static const char *kKnown[] = {"avatar-01", "avatar-02", "avatar-03", "avatar-04",
                                   "avatar-05", "avatar-06", "avatar-07", "avatar-08"};
    int i;
    if (id == 0 || id[0] == '\0') {
        return 0;
    }
    for (i = 0; i < 8; ++i) {
        if (strcmp(id, kKnown[i]) == 0) {
            return 1;
        }
    }
    return 0;
}

} /* namespace */

RecipientPicker::RecipientPicker()
{
    reset();
}

void RecipientPicker::reset()
{
    memset(this, 0, sizeof(*this));
}

int RecipientPicker::load_members(const HangoutMember *members, int count, const char *self_user_id)
{
    int i;
    int out = 0;
    selected_order_count_ = 0;
    if (members == 0 || count <= 0) {
        member_count_ = 0;
        return 0;
    }
    for (i = 0; i < count && out < kMaxRecipients; ++i) {
        if (self_user_id != 0 && strcmp(members[i].user_id, self_user_id) == 0) {
            continue;
        }
        members_[out] = members[i];
        members_[out].selected = 0;
        members_[out].focused = out == 0 ? 1 : 0;
        out += 1;
    }
    member_count_ = out;
    return out;
}

int RecipientPicker::refresh_members(const HangoutMember *members, int count, const char *self_user_id)
{
    char kept[kMaxRecipients][kMaxUserId];
    char focus_id[kMaxUserId];
    int kept_count = 0;
    int i;
    int loaded;
    int focus_index;

    memset(kept, 0, sizeof(kept));
    memset(focus_id, 0, sizeof(focus_id));
    focus_index = focused_index();
    if (focus_index >= 0 && focus_index < member_count_) {
        strncpy(focus_id, members_[focus_index].user_id, kMaxUserId - 1);
    }
    for (i = 0; i < selected_order_count_ && kept_count < kMaxRecipients; ++i) {
        int idx = selected_order_[i];
        if (idx < 0 || idx >= member_count_ || members_[idx].selected == 0) {
            continue;
        }
        strncpy(kept[kept_count], members_[idx].user_id, kMaxUserId - 1);
        kept_count += 1;
    }
    loaded = load_members(members, count, self_user_id);
    for (i = 0; i < kept_count; ++i) {
        int idx = index_of_user(kept[i]);
        if (idx >= 0) {
            toggle_selection(idx);
        }
    }
    if (focus_id[0] != '\0') {
        int idx = index_of_user(focus_id);
        if (idx >= 0) {
            set_focus(idx);
        }
    }
    return loaded;
}

void RecipientPicker::toggle_selection(int index)
{
    int i;
    if (index < 0 || index >= member_count_) {
        return;
    }
    if (members_[index].selected) {
        members_[index].selected = 0;
        for (i = 0; i < selected_order_count_; ++i) {
            int j;
            if (selected_order_[i] != index) {
                continue;
            }
            for (j = i; j + 1 < selected_order_count_; ++j) {
                selected_order_[j] = selected_order_[j + 1];
            }
            selected_order_count_ -= 1;
            break;
        }
        return;
    }
    members_[index].selected = 1;
    if (selected_order_count_ < kMaxRecipients) {
        selected_order_[selected_order_count_] = index;
        selected_order_count_ += 1;
    }
}

void RecipientPicker::set_focus(int index)
{
    int i;
    if (index < 0 || index >= member_count_) {
        return;
    }
    for (i = 0; i < member_count_; ++i) {
        members_[i].focused = i == index ? 1 : 0;
    }
}

int RecipientPicker::member_count() const
{
    return member_count_;
}

int RecipientPicker::is_selected(int index) const
{
    if (index < 0 || index >= member_count_) {
        return 0;
    }
    return members_[index].selected ? 1 : 0;
}

const char *RecipientPicker::user_id_at(int index) const
{
    if (index < 0 || index >= member_count_) {
        return "";
    }
    return members_[index].user_id;
}

int RecipientPicker::selected_count() const
{
    int count = 0;
    int i;
    for (i = 0; i < member_count_; ++i) {
        if (members_[i].selected) {
            count += 1;
        }
    }
    return count;
}

int RecipientPicker::focused_index() const
{
    int i;
    for (i = 0; i < member_count_; ++i) {
        if (members_[i].focused) {
            return i;
        }
    }
    return -1;
}

int RecipientPicker::index_of_user(const char *user_id) const
{
    int i;
    if (user_id == 0 || user_id[0] == '\0') {
        return -1;
    }
    for (i = 0; i < member_count_; ++i) {
        if (strcmp(members_[i].user_id, user_id) == 0) {
            return i;
        }
    }
    return -1;
}

void RecipientPicker::header_text(char *out, int cap) const
{
    char full[384];
    char first[kMaxName];
    int i;
    int written = 0;
    int first_idx = -1;
    if (out == 0 || cap <= 0) {
        return;
    }
    out[0] = '\0';
    if (cap == 1 || selected_order_count_ <= 0) {
        copy_text(out, cap, "Send a message to ...");
        return;
    }
    full[0] = '\0';
    append_text(full, (int)sizeof(full), "To");
    for (i = 0; i < selected_order_count_; ++i) {
        int idx = selected_order_[i];
        if (idx < 0 || idx >= member_count_ || members_[idx].selected == 0) {
            continue;
        }
        if (written == 0) {
            first_idx = idx;
            append_text(full, (int)sizeof(full), " ");
        } else {
            append_text(full, (int)sizeof(full), ", ");
        }
        append_text(full, (int)sizeof(full), members_[idx].display_name);
        written += 1;
    }
    if (written == 0) {
        copy_text(out, cap, "Send a message to ...");
        return;
    }
    if ((int)strlen(full) > kHeaderLimit) {
        int others = written - 1;
        const char *name = "";
        if (first_idx >= 0 && first_idx < member_count_) {
            name = members_[first_idx].display_name;
        }
        first_token(name, first, (int)sizeof(first));
        snprintf(full, sizeof(full), "To %s +%d", first, others);
    }
    copy_text(out, cap, full);
}

int RecipientPicker::copy_selection(char ids[][kMaxUserId], char names[][kMaxName], int cap) const
{
    int count = 0;
    int i;
    if (ids == 0 || cap <= 0) {
        return 0;
    }
    for (i = 0; i < selected_order_count_ && count < cap; ++i) {
        int idx = selected_order_[i];
        if (idx < 0 || idx >= member_count_ || members_[idx].selected == 0) {
            continue;
        }
        strncpy(ids[count], members_[idx].user_id, kMaxUserId - 1);
        ids[count][kMaxUserId - 1] = '\0';
        if (names != 0) {
            strncpy(names[count], members_[idx].display_name, kMaxName - 1);
            names[count][kMaxName - 1] = '\0';
        }
        count += 1;
    }
    return count;
}

int RecipientPicker::build_frozen_recipients(char ids[][kMaxUserId], int cap) const
{
    int count = 0;
    int i;
    for (i = 0; i < member_count_ && count < cap; ++i) {
        if (!members_[i].selected) {
            continue;
        }
        strncpy(ids[count], members_[i].user_id, kMaxUserId - 1);
        ids[count][kMaxUserId - 1] = '\0';
        count += 1;
    }
    return count;
}

RecipientCard RecipientPicker::card_for_index(int index) const
{
    if (index < 0 || index >= member_count_) {
        RecipientCard empty;
        memset(&empty, 0, sizeof(empty));
        return empty;
    }
    return resolve_recipient_fallback(members_[index]);
}

int RecipientPicker::empty_copy_shown() const
{
    return member_count_ == 0;
}

RecipientCard resolve_recipient_fallback(const HangoutMember &member)
{
    RecipientCard card;
    memset(&card, 0, sizeof(card));
    strncpy(card.display_name, member.display_name, sizeof(card.display_name) - 1);
    strncpy(card.avatar_id, member.avatar_id, sizeof(card.avatar_id) - 1);
    strncpy(card.card_background_id, member.card_background_id, sizeof(card.card_background_id) - 1);
    if (card.avatar_id[0] == '\0' || avatar_known(card.avatar_id) == 0) {
        card.use_initials_fallback = 1;
    }
    if (card.card_background_id[0] == '\0' || card_background_known(card.card_background_id) == 0) {
        card.use_card_fallback = 1;
        strncpy(card.card_background_id, "card-bg-01", sizeof(card.card_background_id) - 1);
    }
    return card;
}

} /* namespace fl_product_send */
