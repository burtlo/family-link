#pragma once

#include "lvgl.h"
#include <cstdint>
#include <memory>

namespace product_ui {
/* V2_MEM sample tagged <snake_name>_<phase>; must run on the LVGL task. */
void probeController(const char *name, const char *phase, int32_t detail);

class RootViewController;
class TransitionCoordinator;
enum class CoverDirection { Forward, Reverse };

class ViewController {
public:
    virtual ~ViewController();
    bool loadView(lv_obj_t *parent);
    void unloadView();
    lv_obj_t *view() const { return root_; }
    bool loaded() const { return loaded_; }
    bool appeared() const { return appeared_; }
    bool transitioning() const { return appearance_ != Appearance::None; }
    virtual const char *name() const = 0;
    virtual void render() {}
    virtual bool handlesBoot() const { return false; }
    virtual bool handlesRed() const { return false; }
    virtual void onBoot() {}
    virtual void onRed() {}
protected:
    virtual bool createView(lv_obj_t *parent);
    virtual void viewDidLoad() {}
    virtual void viewWillAppear() {}
    virtual void viewDidAppear() {}
    virtual void viewWillDisappear() {}
    virtual void viewDidDisappear() {}
    virtual void viewWillUnload() {}
    // Clear all derived LVGL handles immediately after the owned tree is deleted.
    virtual void viewWasRemoved() {}
    lv_obj_t *root_ = nullptr;
private:
    friend class RootViewController;
    friend class TransitionCoordinator;
    void beginAppearanceTransition(bool appearing, bool animated);
    void endAppearanceTransition();
    void removeView();
    bool loaded_ = false;
    bool appeared_ = false;
    enum class Appearance { None, Appearing, Disappearing } appearance_ = Appearance::None;
};

class NoDestinationViewController final : public ViewController {
public:
    const char *name() const override { return "NoDestination"; }
};

struct SnapshotAllocator {
    void *(*allocate)(size_t);
    void (*release)(void *);
    // Optional fault-injection seam; production uses lv_image_create.
    lv_obj_t *(*createImage)(lv_obj_t *);
};

class RootViewController {
public:
    explicit RootViewController(lv_obj_t *contentRoot, SnapshotAllocator allocator = {});
    ~RootViewController();
    bool replace(std::unique_ptr<ViewController> next, bool animated = true,
                 CoverDirection direction = CoverDirection::Forward);
    void clear();
    void finishTransition();
    bool busy() const;
    ViewController *current() const { return current_.get(); }
    void render();
    void dispatchBoot();
    void dispatchRed();
    lv_obj_t *contentRoot() const { return content_root_; }
    lv_obj_t *snapshotImage() const;
private:
    friend class TransitionCoordinator;
    void beginAppearance(ViewController &controller, bool appearing, bool animated);
    void endAppearance(ViewController &controller);
    lv_obj_t *content_root_;
    std::unique_ptr<ViewController> current_;
    std::unique_ptr<TransitionCoordinator> coordinator_;
    bool destroying_ = false;
};
} // namespace product_ui
