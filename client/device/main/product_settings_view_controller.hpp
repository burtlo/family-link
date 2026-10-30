#pragma once

#include "product_settings_service.hpp"
#include "product_split_pane_view_controller.hpp"

namespace product_ui
{
class SettingsViewController final : public SplitPaneViewController
{
  public:
    explicit SettingsViewController(SettingsService &service) : service_(service) {}
    const char *name() const override { return "Settings"; }
    bool handlesBoot() const override { return true; }
    void onBoot() override;
    void render() override;
    lv_obj_t *slider() const { return slider_; }
    lv_obj_t *heading() const { return heading_; }

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;

  private:
    static void onValue(lv_event_t *event);
    SettingsService &service_;
    lv_obj_t *heading_ = nullptr;
    lv_obj_t *slider_ = nullptr;
};
} // namespace product_ui
