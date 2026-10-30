#pragma once

#include "product_delivery_service.hpp"
#include "product_display_surface.hpp"
#include "product_view_controller.hpp"

namespace product_ui
{

enum class SketchIntent { Boot,
                          Red };
struct SketchNavigation {
    void *context = nullptr;
    void (*request)(void *, SketchIntent) = nullptr;
};

// Canvas, palette, and ring. Capture and upload stay on the delivery service.
class SketchViewController final : public ViewController
{
  public:
    SketchViewController(DeliveryService &delivery, DisplaySurfaceBroker &surfaces, SketchNavigation navigation)
        : delivery_(delivery), surfaces_(surfaces), navigation_(navigation) {}
    const char *name() const override
    {
        return "Sketch";
    }
    void render() override;
    bool handlesBoot() const override
    {
        return delivery_.sketchBoot();
    }
    bool handlesRed() const override
    {
        return delivery_.sketchRed();
    }
    void onBoot() override;
    void onRed() override;
    lv_obj_t *canvas() const
    {
        return canvas_;
    }
    lv_obj_t *swatch(int index) const;

  protected:
    bool createView(lv_obj_t *parent) override;
    void viewWillUnload() override;
    void viewWasRemoved() override;

  private:
    static void onPointer(lv_event_t *event);
    void paintMotion();
    void outlineSwatch();
    DeliveryService &delivery_;
    DisplaySurfaceBroker &surfaces_;
    SketchNavigation navigation_;
    SurfaceLease lease_{};
    lv_obj_t *reveal_ = nullptr;
    lv_obj_t *canvas_ = nullptr;
    lv_obj_t *rail_ = nullptr;
    lv_obj_t *ring_ = nullptr;
    lv_obj_t *swatches_[6]{};
    int outlined_ = -1;
};

} // namespace product_ui
