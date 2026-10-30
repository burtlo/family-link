#pragma once

#include "product_login_service.hpp"
#include "product_split_pane_view_controller.hpp"

namespace product_ui
{

class PinEntryViewController final : public SplitPaneViewController
{
  public:
    explicit PinEntryViewController(LoginService &service) : service_(service) {}
    const char *name() const override { return "PinEntry"; }
    void render() override;
    bool handlesBoot() const override { return true; }
    void onBoot() override;
    bool enterDigit(char digit);
    lv_obj_t *keyAt(int digit) const;
    lv_obj_t *identityLabel() const { return identity_; }
    lv_obj_t *marksLabel() const { return checking_; }
    lv_obj_t *statusLabel() const { return status_; }

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;

  private:
    struct Key { PinEntryViewController *owner = nullptr; char digit = 0; } key_contexts_[9];
    static void onKey(lv_event_t *event);
    void displayBody(bool status);
    LoginService &service_;
    lv_obj_t *identity_ = nullptr;
    lv_obj_t *prefix_ = nullptr;
    lv_obj_t *avatar_ = nullptr;
    lv_obj_t *initials_ = nullptr;
    lv_obj_t *marks_[4]{};
    lv_obj_t *checking_ = nullptr;
    lv_obj_t *pad_ = nullptr;
    lv_obj_t *status_ = nullptr;
    lv_obj_t *keys_[9]{};
    bool status_body_ = false;
    bool desired_status_ = false;
    bool body_initialized_ = false;
    bool fading_ = false;
    uint32_t fade_started_ = 0;
};

class PinCooldownViewController final : public ViewController
{
  public:
    explicit PinCooldownViewController(LoginService &service) : service_(service) {}
    const char *name() const override { return "PinCooldown"; }
    void render() override;
    lv_obj_t *countdownLabel() const { return countdown_; }

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;

  private:
    LoginService &service_;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *countdown_ = nullptr;
};

} // namespace product_ui
