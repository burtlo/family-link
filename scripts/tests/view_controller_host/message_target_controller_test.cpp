#include "product_message_target_service.hpp"
#include "product_message_target_view_controller.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

namespace
{
using namespace product_ui;
int requests;
MessageTargetRequest frozen;
void navigate(void *, MessageTargetRequest intent) { ++requests; frozen = intent; }
void click(lv_obj_t *card)
{
    assert(card);
    lv_obj_send_event(card, LV_EVENT_PRESSED, nullptr);
    lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
}
}

void testMessageTargetControllers(lv_obj_t *content)
{
    using namespace product_ui;
    using namespace fl_product_login;
    UserId users[kMaxUsers]{};
    for (int i = 0; i < kMaxUsers; ++i) {
        std::snprintf(users[i].id, sizeof(users[i].id), "u%d", i);
        std::snprintf(users[i].display_name, sizeof(users[i].display_name), "Person %d", i);
    }
    std::strcpy(users[1].display_name, "Arlo");
    std::strcpy(users[2].display_name, "Lynn");
    MessageTargetService service;
    service.setRoster("u0", users, kMaxUsers);
    service.beginJourney();
    assert(service.snapshot().count == 7 && service.snapshot().selected.count == 0);
    for (int i = 0; i < service.snapshot().count; ++i)
        assert(std::strcmp(service.snapshot().users[i].id, "u0") != 0);
    MessageRecipients recipients;
    assert(!service.freeze(recipients));
    assert(!service.toggle(0, "u0") && !service.toggle(7, "u7"));
    assert(service.toggle(0, "u1"));
    assert(service.focus(1, "u2"));
    assert(service.isSelected("u1") && !service.isSelected("u2"));
    assert(service.toggle(1, "u2"));
    char heading[128];
    service.heading(heading, sizeof(heading));
    assert(std::strcmp(heading, "To Arlo and Lynn") == 0);
    assert(service.toggle(0, "u1") && !service.isSelected("u1"));
    service.heading(heading, sizeof(heading));
    assert(std::strcmp(heading, "To Lynn") == 0);
    service.toggle(0, "u1");
    service.toggle(2, "u3");
    service.heading(heading, sizeof(heading));
    assert(std::strcmp(heading, "To Lynn +2") == 0); // First selected, not first roster entry.
    std::strcpy(users[2].display_name, "Extraordinarilylongfirstname ABC");
    service.setRoster("u0", users, kMaxUsers);
    service.heading(heading, sizeof(heading));
    assert(std::strlen(heading) == 25 && std::strstr(heading, "+2"));
    assert(service.freeze(recipients) && recipients.count == 3);
    UserId replacement[kMaxUsers]{};
    replacement[0] = users[0]; replacement[1] = users[3]; replacement[2] = users[1];
    service.setRoster("u0", replacement, 3); // u2 removed, u1/u3 still selected.
    assert(service.snapshot().selected.count == 2 && !service.isSelected("u2"));
    assert(recipients.count == 3 && std::strcmp(recipients.ids[0], "u2") == 0);
    service.beginJourney();
    assert(!service.snapshot().selected.count);
    service.toggle(0, "u3");
    service.setRoster("u1", users, kMaxUsers); // Different signed-in identity clears selection.
    assert(!service.snapshot().selected.count && service.snapshot().count == 7);
    service.setRoster(nullptr, users, kMaxUsers);
    assert(service.snapshot().count == 0);

    service.setRoster("u0", users, kMaxUsers);
    service.beginJourney();
    requests = 0;
    RootViewController root(content);
    auto *controller = new MessageTargetViewController(service, {nullptr, navigate});
    assert(root.replace(std::unique_ptr<ViewController>(controller), false));
    lv_obj_update_layout(content);
    assert(std::strcmp(lv_label_get_text(controller->heading()), "Send a message to ...") == 0);
    assert(lv_obj_get_child_count(controller->rosterRow()) == 7);
    assert(lv_obj_get_style_outline_width(controller->cardAt(0), LV_PART_MAIN) == 0);
    root.dispatchRed();
    assert(!requests);
    click(controller->cardAt(0));
    assert(service.isSelected("u1"));
    assert(lv_obj_get_style_outline_width(controller->cardAt(0), LV_PART_MAIN) == 2);
    assert(lv_obj_get_child_count(controller->cardAt(0)) == 2); // Avatar and name; no checkmark.
    // Real scroll events change focus only and settle at the nearest center.
    lv_obj_scroll_to_x(controller->rosterRow(), 270, LV_ANIM_OFF);
    lv_obj_send_event(controller->rosterRow(), LV_EVENT_SCROLL_END, nullptr);
    assert(std::strcmp(service.snapshot().focus, "u3") == 0);
    assert(service.snapshot().selected.count == 1 && service.isSelected("u1"));
    assert(lv_obj_get_scroll_x(controller->rosterRow()) == 288);
    // A drag ending over a card cannot also toggle that card.
    auto *card = controller->cardAt(2);
    lv_obj_send_event(card, LV_EVENT_PRESSED, nullptr);
    lv_obj_scroll_to_x(controller->rosterRow(), 144, LV_ANIM_OFF);
    lv_obj_send_event(controller->rosterRow(), LV_EVENT_SCROLL_END, nullptr);
    lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
    assert(service.snapshot().selected.count == 1);
    // Preserve pressed identity while the same LVGL card is rebound by a roster refresh.
    card = controller->cardAt(0);
    lv_obj_send_event(card, LV_EVENT_PRESSED, nullptr);
    service.setRoster("u0", replacement, 3);
    root.render();
    lv_obj_send_event(card, LV_EVENT_CLICKED, nullptr);
    assert(!service.isSelected("u3") && service.isSelected("u1"));
    assert(!controller->selectRecipient(0, "u1"));
    assert(controller->selectRecipient(0, "u3"));
    lv_mem_monitor_t before, after;
    lv_mem_monitor(&before);
    for (int i = 0; i < 30; ++i) root.render();
    lv_mem_monitor(&after);
    assert(before.free_size == after.free_size);
    root.dispatchRed();
    assert(requests == 1 && frozen.destination == MessageTargetDestination::Recording);
    assert(frozen.recipients.count == 2);
    root.dispatchRed(); root.dispatchBoot();
    assert(requests == 1);
    assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
    service.reset();
    assert(frozen.recipients.count == 2 && std::strcmp(frozen.recipients.ids[0], "u1") == 0);

    // Empty roster keeps split geometry and text only; BOOT is still available.
    service.setRoster("u0", users, 1);
    service.beginJourney();
    controller = new MessageTargetViewController(service, {nullptr, navigate});
    assert(root.replace(std::unique_ptr<ViewController>(controller), false));
    lv_obj_update_layout(content);
    assert(lv_obj_get_height(controller->upperSlot()) == 80);
    assert(lv_obj_get_height(controller->lowerSlot()) == 160);
    assert(!lv_obj_is_hidden(controller->statusLabel()));
    assert(std::strcmp(lv_label_get_text(controller->statusLabel()), "No people to message") == 0);
    assert(!controller->cardAt(0));
    assert(!controller->rosterRow());
    assert(lv_obj_get_child_count(controller->lowerSlot()) == 1);
    root.dispatchRed(); assert(requests == 1);
    root.dispatchBoot();
    assert(requests == 2 && frozen.destination == MessageTargetDestination::Inbox);
    assert(frozen.recipients.count == 0);
    assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
    service.setRoster("u0", users, kMaxUsers);
    service.beginJourney();
    controller = new MessageTargetViewController(service, {nullptr, navigate});
    assert(root.replace(std::unique_ptr<ViewController>(controller), false));
    assert(!service.snapshot().selected.count);
}
