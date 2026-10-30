#pragma once

#include "product_inbox_service.hpp"
#include "product_playback_service.hpp"
#include "product_split_pane_view_controller.hpp"

namespace product_ui
{
enum class InboxDestination { Settings,
                              MessageTarget };
struct InboxNavigation {
    void *context = nullptr;
    void (*request)(void *, InboxDestination) = nullptr;
};

class InboxViewController final : public SplitPaneViewController
{
  public:
    InboxViewController(InboxService &service, PlaybackService &playback, InboxNavigation navigation = {})
        : service_(service), playback_(playback), navigation_(navigation) {}
    const char *name() const override
    {
        return "Inbox";
    }
    void render() override;
    bool handlesBoot() const override
    {
        return true;
    }
    bool handlesRed() const override
    {
        return true;
    }
    void onBoot() override;
    void onRed() override;
    bool selectMessage(int index, const char *expectedId);
    lv_obj_t *heading() const
    {
        return heading_;
    }
    lv_obj_t *transportButton() const
    {
        return transport_;
    }
    lv_obj_t *statusLabel() const
    {
        return status_;
    }
    lv_obj_t *carouselRow() const
    {
        return row_;
    }
    lv_obj_t *cardForIndex(int index) const;
    static constexpr int kLiveCards = 3;

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWillUnload() override;
    void viewWasRemoved() override;

  private:
    struct Card {
        InboxViewController *owner = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *avatar = nullptr;
        lv_obj_t *initials = nullptr;
        lv_obj_t *name = nullptr;
        lv_obj_t *duration = nullptr;
        lv_obj_t *canvas = nullptr;
        lv_obj_t *ink = nullptr;
        int index = -1;
        int pressed_index = -1;
        char id[fl_product_inbox::kMaxMessageId]{};
        char pressed_id[fl_product_inbox::kMaxMessageId]{};
    } cards_[kLiveCards];
    static void onPressed(lv_event_t *event);
    static void onClicked(lv_event_t *event);
    static void onScroll(lv_event_t *event);
    bool createCard(Card &card);
    void syncCards(int center);
    void centerSelected();
    void displayBody(bool cards);
    void request(InboxDestination destination);
    void presentTransport(const InboxSnapshot &snapshot);
    void presentInk();
    void detachLease();
    static void onTransport(lv_event_t *event);
    static void onInk(lv_event_t *event);
    InboxService &service_;
    PlaybackService &playback_;
    SurfaceLease lease_{};
    InboxNavigation navigation_;
    lv_obj_t *heading_ = nullptr;
    lv_obj_t *transport_ = nullptr;
    lv_obj_t *transport_label_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *row_ = nullptr;
    lv_obj_t *track_ = nullptr;
    bool programmatic_ = false;
    bool gesture_ = false;
    bool navigation_requested_ = false;
    bool body_initialized_ = false;
    bool showing_cards_ = false;
    bool desired_cards_ = false;
    bool fading_ = false;
    uint32_t fade_started_ = 0;
    int press_scroll_ = 0;
    int rendered_selected_ = -2;
    char rendered_id_[fl_product_inbox::kMaxMessageId]{};
};
} // namespace product_ui
