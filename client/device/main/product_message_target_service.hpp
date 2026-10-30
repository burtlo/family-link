#pragma once

#include "fl_product_login_session.hpp"

namespace product_ui
{
struct MessageRecipients {
    int count = 0;
    char ids[fl_product_login::kMaxUsers][fl_product_login::kMaxId]{};
};
struct MessageTargetSnapshot {
    int count = 0;
    fl_product_login::UserId users[fl_product_login::kMaxUsers]{};
    char focus[fl_product_login::kMaxId]{};
    MessageRecipients selected;
};

// Endpoint lifetime; roster and selection survive view removal, never own UI.
class MessageTargetService
{
  public:
    void setRoster(const char *signedInUser, const fl_product_login::UserId *users, int count);
    void beginJourney();
    void reset();
    bool focus(int index, const char *expectedId);
    bool toggle(int index, const char *expectedId);
    bool isSelected(const char *id) const;
    bool freeze(MessageRecipients &recipients) const;
    void heading(char *out, unsigned capacity) const;
    const MessageTargetSnapshot &snapshot() const { return snapshot_; }

  private:
    int indexOf(const char *id) const;
    MessageTargetSnapshot snapshot_;
    char owner_[fl_product_login::kMaxId]{};
};
} // namespace product_ui
