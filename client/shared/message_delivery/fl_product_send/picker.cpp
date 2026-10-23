#include "fl_product_send_picker.hpp"

#include <string.h>

namespace fl_product_send {

RecipientPicker::RecipientPicker() { reset(); }

void RecipientPicker::reset()
{
    memset(this, 0, sizeof(*this));
}

int RecipientPicker::load_members(const HangoutMember *members, int count, const char *self_user_id)
{
    int i;
    int out = 0;
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

void RecipientPicker::toggle_selection(int index)
{
    if (index < 0 || index >= member_count_) {
        return;
    }
    members_[index].selected = members_[index].selected ? 0 : 1;
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

int RecipientPicker::build_frozen_recipients(char ids[][kMaxUserId], int cap) const
{
    int count = 0;
    int i;
    for (i = 0; i < member_count_ && count < cap; ++i) {
        if (!members_[i].selected) {
            continue;
        }
        strncpy(ids[count], members_[i].user_id, kMaxUserId - 1);
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

int RecipientPicker::empty_copy_shown() const { return member_count_ == 0 || selected_count() == 0; }

RecipientCard resolve_recipient_fallback(const HangoutMember &member)
{
    RecipientCard card;
    memset(&card, 0, sizeof(card));
    strncpy(card.display_name, member.display_name, sizeof(card.display_name) - 1);
    strncpy(card.avatar_id, member.avatar_id, sizeof(card.avatar_id) - 1);
    strncpy(card.card_background_id, member.card_background_id, sizeof(card.card_background_id) - 1);
    if (card.avatar_id[0] == '\0') {
        card.use_initials_fallback = 1;
    }
    if (card.card_background_id[0] == '\0') {
        card.use_card_fallback = 1;
        strncpy(card.card_background_id, "card-bg-01", sizeof(card.card_background_id) - 1);
    }
    return card;
}

} /* namespace fl_product_send */
