#include "product_transition_coordinator.hpp"
#include <cstdint>
#ifdef ESP_PLATFORM
#include "esp_heap_caps.h"
#include "esp_log.h"
#include "esp_memory_utils.h"
#else
#include <cstdlib>
#endif

namespace product_ui {
namespace {
constexpr size_t kPixels = 320u * 240u * 2u;
void *allocateSnapshot(size_t bytes)
{
#ifdef ESP_PLATFORM
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
#else
    return std::malloc(bytes);
#endif
}
void freeSnapshot(void *p)
{
#ifdef ESP_PLATFORM
    heap_caps_free(p);
#else
    std::free(p);
#endif
}
int32_t easeInOutQuint(const lv_anim_t *animation)
{
    const int32_t time = lv_anim_get_time(animation);
    const double t = time > 0 ? static_cast<double>(animation->act_time) / time : 1.0;
    const double u = 1.0 - t;
    const double eased = t < 0.5 ? 16.0 * t * t * t * t * t : 1.0 - 16.0 * u * u * u * u * u;
    return animation->start_value + static_cast<int32_t>((animation->end_value - animation->start_value) * eased);
}
}
TransitionCoordinator::TransitionCoordinator(RootViewController &owner, SnapshotAllocator allocator)
    : owner_(owner), allocator_(allocator)
{
    if(!allocator_.allocate) allocator_.allocate = allocateSnapshot;
    if(!allocator_.release) allocator_.release = freeSnapshot;
    if(!allocator_.createImage) allocator_.createImage = lv_image_create;
}
TransitionCoordinator::~TransitionCoordinator() { finishNow(); releaseSnapshot(); }

bool TransitionCoordinator::capture(lv_obj_t *root, bool attach)
{
    allocation_ = allocator_.allocate(kPixels + LV_DRAW_BUF_ALIGN - 1);
    if(!allocation_) return false;
    const uintptr_t raw = reinterpret_cast<uintptr_t>(allocation_);
    auto *pixels = reinterpret_cast<void *>((raw + LV_DRAW_BUF_ALIGN - 1) & ~(uintptr_t)(LV_DRAW_BUF_ALIGN - 1));
    if(lv_draw_buf_init(&draw_buf_, 320, 240, LV_COLOR_FORMAT_RGB565, 640, pixels, kPixels) != LV_RESULT_OK ||
       lv_snapshot_take_to_draw_buf(root, LV_COLOR_FORMAT_RGB565, &draw_buf_) != LV_RESULT_OK) {
        releaseSnapshot();
        return false;
    }
    descriptor_.header = draw_buf_.header;
    descriptor_.data_size = kPixels;
    descriptor_.data = static_cast<const uint8_t *>(pixels);
    if(attach && !attachImage()) { releaseSnapshot(); return false; }
    return true;
}

bool TransitionCoordinator::attachImage()
{
    image_ = allocator_.createImage(owner_.content_root_);
    if(!image_) return false;
    lv_image_set_src(image_, &descriptor_);
    lv_obj_set_pos(image_, 0, 0);
    lv_obj_set_clickable(image_, false);
    lv_obj_set_scrollable(image_, false);
    return true;
}

void TransitionCoordinator::releaseSnapshot()
{
    if(image_) { lv_obj_delete(image_); image_ = nullptr; }
    if(allocation_) { allocator_.release(allocation_); allocation_ = nullptr; }
    descriptor_ = {};
    draw_buf_ = {};
}

bool TransitionCoordinator::replace(std::unique_ptr<ViewController> next, bool animated, CoverDirection direction)
{
    if(busy_ || !next || !owner_.content_root_) return false;
    // Lifecycle methods are user code and may synchronously request navigation.
    // Reserve the whole transaction before invoking any of them.
    busy_ = true;
    bool has_outgoing = owner_.current_ != nullptr;
#ifdef ESP_PLATFORM
    if (animated && has_outgoing) ESP_LOGI("app_main", "vc_transition_begin from=%s to=%s", owner_.current_->name(), next->name());
#endif
    if(has_outgoing) {
        owner_.beginAppearance(*owner_.current_, false, animated);
        if(animated) probeController("Transition", "snapshot_before", 0);
        // Reverse motion attaches its image after the incoming view loads so the
        // outgoing pixels sit above it without reordering siblings.
        const bool captured = !animated || capture(owner_.current_->view(), direction == CoverDirection::Forward);
        if(animated) {
#ifdef ESP_PLATFORM
            const char *region = !allocation_ ? "none" : esp_ptr_external_ram(allocation_) ? "psram" : "internal";
            ESP_LOGI("app_main", "vc_snapshot ok=%d region=%s bytes=%u", captured ? 1 : 0, region,
                     captured ? static_cast<unsigned>(kPixels) : 0u);
#endif
            probeController("Transition", "snapshot_after", captured ? static_cast<int32_t>(kPixels) : 0);
        }
        if(!captured) {
            owner_.beginAppearance(*owner_.current_, true, false);
            owner_.endAppearance(*owner_.current_);
            busy_ = false;
            return false;
        }
        owner_.current_->removeView();
        owner_.endAppearance(*owner_.current_);
        owner_.current_->unloadView();
        owner_.current_.reset();
    }
    if(!next->loadView(owner_.content_root_)) {
        releaseSnapshot();
        busy_ = false;
        return false;
    }
    owner_.current_ = std::move(next);
    owner_.beginAppearance(*owner_.current_, true, animated && has_outgoing);
    lv_obj_t *root = owner_.current_->view();
    clickable_ = lv_obj_is_clickable(root);
    const bool reverse = direction == CoverDirection::Reverse;
    // Without its image a reverse reveal has nothing to move; show the
    // destination immediately rather than fail an already-committed swap.
    if(animated && has_outgoing && reverse && !attachImage()) animated = false;
    if(animated && has_outgoing) {
        animating_ = true;
        started_at_ = lv_tick_get();
        lv_obj_set_clickable(root, false);
        lv_obj_set_y(root, reverse ? 0 : 240);
        // A reverse reveal moves the fixed outgoing pixels, never its deleted
        // controller tree.
        animated_object_ = reverse ? image_ : root;
        // The product owns one touch input. Disable device dispatch for the
        // whole subtree; changing only root CLICKABLE does not block children.
        lv_indev_enable(nullptr, false);
        lv_anim_t animation;
        lv_anim_init(&animation);
        lv_anim_set_var(&animation, animated_object_);
        lv_anim_set_exec_cb(&animation, animateY);
        lv_anim_set_values(&animation, reverse ? 0 : 240, reverse ? 240 : 0);
        lv_anim_set_duration(&animation, 400);
        lv_anim_set_path_cb(&animation, easeInOutQuint);
        lv_anim_set_user_data(&animation, this);
        lv_anim_set_completed_cb(&animation, animationDone);
        if(!lv_anim_start(&animation)) finishNow();
    }
    else {
        lv_obj_set_y(root, 0);
        owner_.endAppearance(*owner_.current_);
        releaseSnapshot();
        busy_ = false;
    }
    return true;
}

void TransitionCoordinator::animateY(void *obj, int32_t y) { lv_obj_set_y(static_cast<lv_obj_t *>(obj), y); }
void TransitionCoordinator::animationDone(lv_anim_t *animation)
{
    static_cast<TransitionCoordinator *>(lv_anim_get_user_data(animation))->complete();
}
void TransitionCoordinator::complete()
{
    if(!animating_) return;
    animating_ = false;
    animated_object_ = nullptr;
    if(owner_.current_) {
        lv_obj_set_y(owner_.current_->view(), 0);
        lv_obj_set_clickable(owner_.current_->view(), clickable_);
        owner_.endAppearance(*owner_.current_);
    }
    releaseSnapshot();
    lv_indev_enable(nullptr, true);
    const uint32_t elapsed = lv_tick_get() - started_at_;
#ifdef ESP_PLATFORM
    ESP_LOGI("app_main", "vc_transition_end to=%s ms=%u", owner_.current_ ? owner_.current_->name() : "None", static_cast<unsigned>(elapsed));
#endif
    probeController("Transition", "complete", static_cast<int32_t>(elapsed));
    busy_ = false;
}
void TransitionCoordinator::finishNow()
{
    if(!animating_) return;
    if(animated_object_) lv_anim_delete(animated_object_, animateY);
    complete();
}
void TransitionCoordinator::clear()
{
    finishNow();
    if (busy_ || !owner_.current_) return;
    busy_ = true;
    owner_.beginAppearance(*owner_.current_, false, false);
    owner_.current_->removeView();
    owner_.endAppearance(*owner_.current_);
    owner_.current_->unloadView();
    owner_.current_.reset();
    busy_ = false;
}
} // namespace product_ui
