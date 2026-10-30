#pragma once

#include "product_login_service.hpp"
#include "product_split_pane_view_controller.hpp"

namespace product_ui
{

enum class LoginDestination { PinEntry };

struct LoginNavigation {
    void *context = nullptr;
    void (*request)(void *, LoginDestination, const char *) = nullptr;
};

class LoginViewController final : public SplitPaneViewController
{
  public:
    LoginViewController(LoginService &service, LoginNavigation navigation);
    const char *name() const override { return "Login"; }
    void render() override;
    // Validates the card identity against the current service snapshot; also
    // used by host tests to exercise stale activation without a fake LVGL.
    bool selectUser(int index, const char *expectedId);
    lv_obj_t *heading() const { return heading_; }
    lv_obj_t *statusLabel() const { return status_; }
    lv_obj_t *rosterRow() const { return row_; }
    lv_obj_t *cardAt(int index) const;
    unsigned widgetRevision() const { return widget_revision_; }

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;

  private:
    struct Card {
        LoginViewController *owner = nullptr;
        lv_obj_t *root = nullptr;
        lv_obj_t *initials = nullptr;
        lv_obj_t *name = nullptr;
        fl_product_login::UserId user{};
        int index = -1;
    };
    static void onPressed(lv_event_t *event);
    static void onClicked(lv_event_t *event);
    static void onScroll(lv_event_t *event);
    bool createCard(Card &card);
    void clearCards();
    void syncCards(const LoginSnapshot &snapshot);
    void setLabel(lv_obj_t *label, const char *text);
    void setHidden(lv_obj_t *object, bool hidden);
    LoginService &service_;
    LoginNavigation navigation_;
    lv_obj_t *heading_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *row_ = nullptr;
    Card cards_[fl_product_login::kMaxUsers]{};
    bool gesture_ = false;
    bool programmatic_scroll_ = false;
    bool navigation_requested_ = false;
    bool rendering_ = false;
    int press_scroll_ = 0;
    unsigned widget_revision_ = 0;
    char rendered_focus_[fl_product_login::kMaxId]{};
};

} // namespace product_ui
