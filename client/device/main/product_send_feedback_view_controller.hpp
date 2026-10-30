#pragma once

#include "product_delivery_service.hpp"
#include "product_split_pane_view_controller.hpp"

namespace product_ui
{

struct SendFeedbackNavigation {
    void *context = nullptr;
    void (*leave)(void *) = nullptr;
};

class SendFeedbackViewController final : public SplitPaneViewController
{
  public:
    SendFeedbackViewController(DeliveryService &delivery, SendFeedbackNavigation navigation)
        : delivery_(delivery), navigation_(navigation) {}
    const char *name() const override
    {
        return "SendFeedback";
    }
    void render() override;
    bool handlesBoot() const override
    {
        return delivery_.feedbackBoot();
    }
    bool handlesRed() const override
    {
        return false;
    }
    void onBoot() override;
    lv_obj_t *titleLabel() const
    {
        return title_;
    }
    lv_obj_t *subtitleLabel() const
    {
        return subtitle_;
    }
    lv_obj_t *bodyLabel() const
    {
        return body_;
    }

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWasRemoved() override;

  private:
    DeliveryService &delivery_;
    SendFeedbackNavigation navigation_;
    lv_obj_t *title_ = nullptr;
    lv_obj_t *subtitle_ = nullptr;
    lv_obj_t *body_ = nullptr;
};

} // namespace product_ui
