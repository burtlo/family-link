#pragma once

#include "product_message_target_service.hpp"
#include "product_split_pane_view_controller.hpp"

namespace product_ui
{
enum class MessageTargetDestination { Inbox, Recording };
struct MessageTargetRequest {
    MessageTargetDestination destination = MessageTargetDestination::Inbox;
    MessageRecipients recipients;
};
struct MessageTargetNavigation {
    void *context = nullptr;
    // Passed by value: the queued intent owns its frozen IDs independently of
    // this controller, the service roster, and any later picker journey.
    void (*request)(void *, MessageTargetRequest) = nullptr;
};

class MessageTargetViewController final : public SplitPaneViewController
{
  public:
    MessageTargetViewController(MessageTargetService &service, MessageTargetNavigation navigation = {})
        : service_(service), navigation_(navigation) {}
    const char *name() const override { return "MessageTarget"; }
    void render() override;
    bool handlesBoot() const override { return true; }
    bool handlesRed() const override { return true; }
    void onBoot() override;
    void onRed() override;
    bool selectRecipient(int index, const char *expectedId);
    lv_obj_t *heading() const { return heading_; }
    lv_obj_t *statusLabel() const { return status_; }
    lv_obj_t *rosterRow() const { return row_; }
    lv_obj_t *cardAt(int index) const;

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;

  private:
    struct Card {
        MessageTargetViewController *owner = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *initials = nullptr;
        lv_obj_t *name = nullptr;
        fl_product_login::UserId user{};
        int index = -1;
        int pressed_index = -1;
        char pressed_id[fl_product_login::kMaxId]{};
    } cards_[fl_product_login::kMaxUsers];
    bool createCard(Card &card);
    bool createRow();
    static void onPressed(lv_event_t *event);
    static void onClicked(lv_event_t *event);
    static void onScroll(lv_event_t *event);
    void request(MessageTargetRequest intent);
    MessageTargetService &service_;
    MessageTargetNavigation navigation_;
    lv_obj_t *heading_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *row_ = nullptr;
    bool rendering_ = false;
    bool programmatic_ = false;
    bool gesture_ = false;
    bool navigation_requested_ = false;
    int press_scroll_ = 0;
    int rendered_focus_index_ = -1;
    char rendered_focus_[fl_product_login::kMaxId]{};
};
} // namespace product_ui
