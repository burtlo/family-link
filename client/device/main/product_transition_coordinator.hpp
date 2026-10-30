#pragma once
#include "product_view_controller.hpp"

namespace product_ui {
class TransitionCoordinator {
public:
    TransitionCoordinator(RootViewController &owner, SnapshotAllocator allocator);
    ~TransitionCoordinator();
    bool replace(std::unique_ptr<ViewController> next, bool animated, CoverDirection direction);
    void finishNow();
    void clear();
    bool busy() const { return busy_; }
    lv_obj_t *image() const { return image_; }
private:
    bool capture(lv_obj_t *root, bool attach);
    bool attachImage();
    void releaseSnapshot();
    void complete();
    static void animateY(void *obj, int32_t y);
    static void animationDone(lv_anim_t *animation);
    RootViewController &owner_;
    SnapshotAllocator allocator_;
    void *allocation_ = nullptr;
    lv_draw_buf_t draw_buf_{};
    lv_image_dsc_t descriptor_{};
    lv_obj_t *image_ = nullptr;
    lv_obj_t *animated_object_ = nullptr;
    bool busy_ = false;
    bool animating_ = false;
    bool clickable_ = false;
    uint32_t started_at_ = 0;
};
} // namespace product_ui
