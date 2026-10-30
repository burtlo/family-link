#include "product_message_target_service.hpp"

#include <cstdio>
#include <cstring>

namespace product_ui
{
namespace
{
template <unsigned N> void copy(char (&out)[N], const char *in)
{
    std::snprintf(out, N, "%s", in ? in : "");
}
// Count and truncate at UTF-8 character boundaries, never in a continuation byte.
unsigned characters(const char *text)
{
    unsigned count = 0;
    for (; *text; ++text) if ((static_cast<unsigned char>(*text) & 0xc0) != 0x80) ++count;
    return count;
}
void truncate(char *text, unsigned limit)
{
    unsigned count = 0;
    for (char *p = text; *p; ++p) {
        if ((static_cast<unsigned char>(*p) & 0xc0) != 0x80 && count++ == limit) { *p = 0; return; }
    }
}
}

int MessageTargetService::indexOf(const char *id) const
{
    if (!id) return -1;
    for (int i = 0; i < snapshot_.count; ++i)
        if (std::strcmp(id, snapshot_.users[i].id) == 0) return i;
    return -1;
}
void MessageTargetService::reset() { snapshot_ = {}; owner_[0] = 0; }
void MessageTargetService::beginJourney()
{
    snapshot_.selected = {};
    copy(snapshot_.focus, snapshot_.count ? snapshot_.users[0].id : "");
}
void MessageTargetService::setRoster(const char *signedInUser, const fl_product_login::UserId *users, int count)
{
    if (!signedInUser || !*signedInUser) { reset(); return; }
    if (std::strcmp(owner_, signedInUser) != 0) { reset(); copy(owner_, signedInUser); }
    const auto prior = snapshot_;
    snapshot_.count = 0;
    if (!users || count < 0) count = 0;
    if (count > fl_product_login::kMaxUsers) count = fl_product_login::kMaxUsers;
    for (int i = 0; i < count; ++i) {
        if (!users[i].id[0] || std::strcmp(users[i].id, owner_) == 0 || indexOf(users[i].id) >= 0) continue;
        snapshot_.users[snapshot_.count++] = users[i];
    }
    snapshot_.selected = {};
    for (int i = 0; i < prior.selected.count; ++i) {
        if (indexOf(prior.selected.ids[i]) < 0) continue;
        copy(snapshot_.selected.ids[snapshot_.selected.count++], prior.selected.ids[i]);
    }
    copy(snapshot_.focus, indexOf(prior.focus) >= 0 ? prior.focus :
         snapshot_.count ? snapshot_.users[0].id : "");
}
bool MessageTargetService::focus(int index, const char *expectedId)
{
    if (index < 0 || index >= snapshot_.count || !expectedId ||
        std::strcmp(expectedId, snapshot_.users[index].id) != 0) return false;
    copy(snapshot_.focus, expectedId);
    return true;
}
bool MessageTargetService::isSelected(const char *id) const
{
    if (!id) return false;
    for (int i = 0; i < snapshot_.selected.count; ++i)
        if (std::strcmp(id, snapshot_.selected.ids[i]) == 0) return true;
    return false;
}
bool MessageTargetService::toggle(int index, const char *expectedId)
{
    if (!focus(index, expectedId)) return false;
    auto &selected = snapshot_.selected;
    for (int i = 0; i < selected.count; ++i) {
        if (std::strcmp(expectedId, selected.ids[i]) != 0) continue;
        for (int j = i + 1; j < selected.count; ++j) copy(selected.ids[j - 1], selected.ids[j]);
        selected.ids[--selected.count][0] = 0;
        return true;
    }
    if (selected.count >= fl_product_login::kMaxUsers) return false;
    copy(selected.ids[selected.count++], expectedId);
    return true;
}
bool MessageTargetService::freeze(MessageRecipients &recipients) const
{
    recipients = {};
    for (int i = 0; i < snapshot_.selected.count; ++i)
        if (indexOf(snapshot_.selected.ids[i]) >= 0)
            copy(recipients.ids[recipients.count++], snapshot_.selected.ids[i]);
    return recipients.count > 0;
}
void MessageTargetService::heading(char *out, unsigned capacity) const
{
    if (!out || !capacity) return;
    if (!snapshot_.selected.count) { std::snprintf(out, capacity, "Send a message to ..."); return; }
    char names[320] = "To ";
    for (int i = 0; i < snapshot_.selected.count; ++i) {
        const int index = indexOf(snapshot_.selected.ids[i]);
        if (index < 0) continue;
        const auto used = std::strlen(names);
        std::snprintf(names + used, sizeof(names) - used, "%s%s",
                      i ? (i == snapshot_.selected.count - 1 ? " and " : ", ") : "",
                      snapshot_.users[index].display_name);
    }
    if (characters(names) <= 25) { std::snprintf(out, capacity, "%s", names); return; }
    char first[32];
    copy(first, snapshot_.users[indexOf(snapshot_.selected.ids[0])].display_name);
    if (char *space = std::strchr(first, ' ')) *space = 0;
    char suffix[16]{};
    if (snapshot_.selected.count > 1) std::snprintf(suffix, sizeof(suffix), " +%d", snapshot_.selected.count - 1);
    truncate(first, 25 - 3 - characters(suffix));
    std::snprintf(out, capacity, "To %s%s", first, suffix);
}
} // namespace product_ui
