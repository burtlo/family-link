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
    /* Fresh entry. Replaces the carousel, drops the signed-in user, adds no
       Everyone entry, and clears selection. Focus starts on the first card. */
    int load_members(const HangoutMember *members, int count, const char *self_user_id);
    /* Roster update while the picker stays open. Same omission rules as
       load_members, but selection is kept: a person who left is removed from
       the carousel and from the selection, people still present stay selected
       in the same selection order, and new members appear unselected. Does
       not clear every selection. */
    int refresh_members(const HangoutMember *members, int count, const char *self_user_id);
    /* Changes selection only. Does not move focus. */
    void toggle_selection(int index);
    /* Changes the centered card only. Does not toggle selection. */
    void set_focus(int index);
    int member_count() const;
    int is_selected(int index) const;
    const char *user_id_at(int index) const;
    int selected_count() const;
    int focused_index() const;
    /* None: "Send a message to ..." (not subject to the 25-character collapse).
       One: "To [display name]". More: "To" plus selected names in selection
       order, separated by ", ". A "To ..." heading longer than 25 characters
       becomes "To [first name] +N". */
    void header_text(char *out, int cap) const;
    int build_frozen_recipients(char ids[][kMaxUserId], int cap) const;
    /* Selection order. names may be null when only ids are needed. */
    int copy_selection(char ids[][kMaxUserId], char names[][kMaxName], int cap) const;
    RecipientCard card_for_index(int index) const;
    int empty_copy_shown() const;

private:
    int index_of_user(const char *user_id) const;

    HangoutMember members_[kMaxRecipients + 1];
    int member_count_;
    int selected_order_[kMaxRecipients];
    int selected_order_count_;
};

RecipientCard resolve_recipient_fallback(const HangoutMember &member);

} /* namespace fl_product_send */

#endif
