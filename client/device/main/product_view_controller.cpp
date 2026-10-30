#include "product_view_controller.hpp"
#include "product_transition_coordinator.hpp"
#include "product_theme.hpp"
#ifdef ESP_PLATFORM
#include "esp_log.h"
#include "esp_timer.h"
#include "product_mem.hpp"
#include <cctype>
#include <cstdio>
#endif

namespace product_ui {
void probeController(const char *name, const char *phase, int32_t detail)
{
#ifdef ESP_PLATFORM
    // "NoDestination" + "load_before" -> "no_destination_load_before".
    char tag[48];
    size_t n = 0;
    for(const char *c = name; *c && n + 2 < sizeof(tag); ++c) {
        if(std::isupper(static_cast<unsigned char>(*c)) && n > 0) tag[n++] = '_';
        tag[n++] = static_cast<char>(std::tolower(static_cast<unsigned char>(*c)));
    }
    std::snprintf(tag + n, sizeof(tag) - n, "_%s", phase);
    product_mem::probe(tag, detail);
#else
    (void)name;
    (void)phase;
    (void)detail;
#endif
}

namespace {
int32_t nowMs()
{
#ifdef ESP_PLATFORM
    return static_cast<int32_t>(esp_timer_get_time() / 1000LL);
#else
    return static_cast<int32_t>(lv_tick_get());
#endif
}
}

ViewController::~ViewController()
{
    if(root_) lv_obj_delete(root_);
}

bool ViewController::createView(lv_obj_t *parent)
{
    root_ = lv_obj_create(parent);
    if(!root_) return false;
    lv_obj_remove_style_all(root_);
    lv_obj_set_size(root_, 320, 240);
    lv_obj_set_pos(root_, 0, 0);
    lv_obj_set_style_bg_color(root_, lv_color_hex(product_theme::Dark::kSurfaceRgb24), 0);
    lv_obj_set_style_bg_opa(root_, LV_OPA_COVER, 0);
    lv_obj_set_style_border_width(root_, 0, 0);
    lv_obj_set_style_pad_all(root_, 0, 0);
    lv_obj_set_scrollable(root_, false);
    return true;
}

bool ViewController::loadView(lv_obj_t *parent)
{
    if(root_) return true;
    probeController(name(), "load_before", 0);
    const int32_t started = nowMs();
    if(!parent || !createView(parent)) {
        unloadView();
        return false;
    }
    viewDidLoad();
    loaded_ = true;
#ifdef ESP_PLATFORM
    ESP_LOGI("app_main", "vc_load name=%s", name());
#endif
    probeController(name(), "load_after", nowMs() - started);
    return true;
}

void ViewController::unloadView()
{
    if(!root_ && !loaded_) return;
#ifdef ESP_PLATFORM
    ESP_LOGI("app_main", "vc_unload name=%s", name());
#endif
    probeController(name(), "will_unload", 0);
    const int32_t started = nowMs();
    viewWillUnload();
    if(root_) lv_obj_delete(root_);
    root_ = nullptr;
    viewWasRemoved();
    loaded_ = false;
    appeared_ = false;
    appearance_ = Appearance::None;
    probeController(name(), "unloaded", nowMs() - started);
}

void ViewController::removeView()
{
    if(root_) lv_obj_delete(root_);
    root_ = nullptr;
    viewWasRemoved();
}

void ViewController::beginAppearanceTransition(bool appearing, bool animated)
{
    (void)animated;
    appearance_ = appearing ? Appearance::Appearing : Appearance::Disappearing;
    if(appearing) viewWillAppear();
    else viewWillDisappear();
}

void ViewController::endAppearanceTransition()
{
    const auto transition = appearance_;
    appearance_ = Appearance::None;
    if(transition == Appearance::Appearing) {
        appeared_ = true;
        viewDidAppear();
    }
    else if(transition == Appearance::Disappearing) {
        appeared_ = false;
        viewDidDisappear();
    }
}

RootViewController::RootViewController(lv_obj_t *parent, SnapshotAllocator allocator)
    : content_root_(parent), coordinator_(new TransitionCoordinator(*this, allocator))
{
    probeController("Root", "created", 0);
}

RootViewController::~RootViewController()
{
    destroying_ = true;
    finishTransition();
    if(current_) {
        beginAppearance(*current_, false, false);
        current_->removeView();
        endAppearance(*current_);
        current_->unloadView();
    }
}

void RootViewController::beginAppearance(ViewController &vc, bool appearing, bool animated)
{
    probeController(vc.name(), appearing ? "will_appear" : "will_disappear", 0);
    vc.beginAppearanceTransition(appearing, animated);
}
void RootViewController::endAppearance(ViewController &vc)
{
    vc.endAppearanceTransition();
    if(vc.appeared()) probeController(vc.name(), "appeared", 0);
}
bool RootViewController::replace(std::unique_ptr<ViewController> next, bool animated)
{
    if(destroying_) return false;
    return coordinator_->replace(std::move(next), animated);
}
void RootViewController::finishTransition() { coordinator_->finishNow(); }
bool RootViewController::busy() const { return coordinator_->busy(); }
lv_obj_t *RootViewController::snapshotImage() const { return coordinator_->image(); }
void RootViewController::render() { if(current_ && !busy()) current_->render(); }
void RootViewController::dispatchBoot()
{
    if(current_ && !busy() && current_->handlesBoot()) current_->onBoot();
}
void RootViewController::dispatchRed()
{
    if(current_ && !busy() && current_->handlesRed()) current_->onRed();
}
} // namespace product_ui
