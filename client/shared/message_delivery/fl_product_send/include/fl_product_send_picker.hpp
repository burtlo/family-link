#ifndef FL_PRODUCT_SEND_PICKER_HPP
#define FL_PRODUCT_SEND_PICKER_HPP

#include "fl_product_send_types.hpp"

namespace fl_product_send {

struct HangoutMember {
    char user_id[kMaxUserId];
    char display_name[kMaxName];
    char avatar_id[kMaxAvatarId];
    char card_background_id[kMaxCardBgId];
    int selected;
    int focused;
};

struct RecipientCard {
    char display_name[kMaxName];
    char avatar_id[kMaxAvatarId];
    char card_background_id[kMaxCardBgId];
    int use_initials_fallback;
    int use_card_fallback;
};

class RecipientPicker {
public:
    RecipientPicker();

    void reset();
    int load_members(const HangoutMember *members, int count, const char *self_user_id);
    void toggle_selection(int index);
    void set_focus(int index);
    int selected_count() const;
    int focused_index() const;
    int build_frozen_recipients(char ids[][kMaxUserId], int cap) const;
    RecipientCard card_for_index(int index) const;
    int empty_copy_shown() const;

private:
    HangoutMember members_[kMaxRecipients + 1];
    int member_count_;
};

RecipientCard resolve_recipient_fallback(const HangoutMember &member);

} /* namespace fl_product_send */

#endif
