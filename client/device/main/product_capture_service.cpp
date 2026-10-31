#include "product_capture_service.hpp"

#include "fl_product_record_lab10.h"

#include <cstring>

#ifdef ESP_PLATFORM
#include "freertos/FreeRTOS.h"
#include "freertos/semphr.h"
#endif

namespace product_ui
{
namespace
{
CaptureService *s_paint = nullptr;
lv_obj_t *s_canvas = nullptr;

void onPixel(const lab10_render_pixel_event_t *event, void *context)
{
    auto *pixels = static_cast<uint16_t *>(context);
    if (!pixels || !event || event->logical_x >= LAB10_LOGICAL_WIDTH || event->logical_y >= LAB10_LOGICAL_HEIGHT)
        return;
    const uint16_t color = lv_color_to_u16(lv_color_hex(lab10_color_rgb(event->color_index)));
    const int scale = LAB10_CANVAS_WIDTH / LAB10_LOGICAL_WIDTH;
    const int x0 = event->logical_x * scale;
    const int y0 = event->logical_y * scale;
    for (int dy = 0; dy < scale; ++dy)
        for (int dx = 0; dx < scale; ++dx)
            pixels[(y0 + dy) * LAB10_CANVAS_WIDTH + x0 + dx] = color;
}

void onInvalidation(const lab10_render_invalidation_t *invalidation, void *)
{
    if (!s_canvas || !invalidation)
        return;
    lv_area_t area;
    const int scale = LAB10_CANVAS_WIDTH / LAB10_LOGICAL_WIDTH;
    lv_obj_get_coords(s_canvas, &area);
    area.x2 = area.x1 + (invalidation->max_x + 1) * scale - 1;
    area.y2 = area.y1 + (invalidation->max_y + 1) * scale - 1;
    area.x1 += invalidation->min_x * scale;
    area.y1 += invalidation->min_y * scale;
    lv_obj_invalidate_area(s_canvas, &area);
}
} // namespace

CaptureService::Lock::Lock(const CaptureService *owner, bool wait) : owner_(owner), held_(owner->lockEnter(wait))
{
}

CaptureService::Lock::~Lock()
{
    if (held_)
        owner_->lockLeave();
}

bool CaptureService::lockEnter(bool wait) const
{
#ifdef ESP_PLATFORM
    ensureLock();
    if (!lock_)
        return false;
    const TickType_t ticks = wait ? portMAX_DELAY : 0;
    return xSemaphoreTakeRecursive(static_cast<SemaphoreHandle_t>(lock_), ticks) == pdTRUE;
#else
    if (external_hold_) {
        if (wait)
            ++blocking_lock_attempts_;
        return false;
    }
    ++lock_depth_;
    return true;
#endif
}

void CaptureService::lockLeave() const
{
#ifdef ESP_PLATFORM
    if (lock_)
        (void)xSemaphoreGiveRecursive(static_cast<SemaphoreHandle_t>(lock_));
#else
    if (lock_depth_ > 0)
        --lock_depth_;
#endif
}

void CaptureService::ensureLock() const
{
#ifdef ESP_PLATFORM
    if (!lock_)
        lock_ = xSemaphoreCreateRecursiveMutex();
#endif
}

void CaptureService::drainTouches()
{
    while (touch_count_ > 0) {
        const QueuedTouch point = touch_queue_[touch_head_];
        touch_head_ = (touch_head_ + 1) % 32;
        --touch_count_;
        (void)session_.route_touch(point.x, point.y, point.phase, 0);
    }
}

void CaptureService::rememberFlags()
{
    cached_input_open_ = session_.capture_active() && !session_.wiping() && !session_.reversing();
    cached_wiping_ = session_.wiping();
    cached_recording_ = session_.capture_active();
    cached_reversing_ = session_.reversing();
    cached_blocks_ = cached_wiping_ || cached_recording_ || session_.storage().has_in_progress();
}

bool CaptureService::holdLockForTest()
{
#ifdef ESP_PLATFORM
    return false;
#else
    if (lock_depth_ != 0)
        return false;
    external_hold_ = true;
    return true;
#endif
}

void CaptureService::releaseLockForTest()
{
#ifndef ESP_PLATFORM
    external_hold_ = false;
#endif
}

int CaptureService::blockingLockAttempts() const
{
    return blocking_lock_attempts_;
}

CaptureService::~CaptureService()
{
#ifdef ESP_PLATFORM
    if (lock_)
        vSemaphoreDelete(static_cast<SemaphoreHandle_t>(lock_));
#endif
}

bool CaptureService::open(const char *path)
{
    Lock lock(this);
    if (!lock.held() || !path || !path[0])
        return false;
    if (!media_.open_path(path, false) && !media_.open_path(path, true))
        return false;
    session_.attach_media(&media_);
    // Directory (320 B) and identity block sit below the 4096-byte media origin.
    open_ = true;
    return true;
}

void CaptureService::setAllocator(void *(*alloc)(size_t, void *), void (*free_fn)(void *, void *), void *context)
{
    session_.set_allocator(alloc, free_fn, context);
}

void CaptureService::setHardware(Arm arm, Failed failed)
{
    arm_ = arm;
    failed_ = failed;
}

bool CaptureService::canStart() const
{
    return open_ && mic_available_;
}

bool CaptureService::canStartSecond()
{
    return open_ && session_.storage().can_start_second_recording();
}

void CaptureService::stageIdentity(const fl_product_record::SendIdentity &identity)
{
    staged_ = identity;
}

bool CaptureService::begin(const char *clientMessageId, int64_t now)
{
    Lock lock(this);
    if (!lock.held() || !canStart())
        return false;
    if (!session_.ensure_capture())
        return false;
    session_.advance_to(now);
    if (!session_.try_start(clientMessageId, fl_product_record::kMicReady)) {
        session_.release_capture();
        return false;
    }
    session_.storage().set_in_progress_identity(staged_);
    return true;
}

void CaptureService::advance(int64_t now)
{
    if (!open_)
        return;
    Lock lock(this, false);
    if (!lock.held())
        return;
    drainTouches();
    session_.advance_to(now);
    if (failed_ && failed_())
        session_.notify_mic_failed();
    const bool live = session_.wiping() || session_.capture_active();
    if (arm_ && live != armed_) {
        arm_(live);
        armed_ = live;
    }
    rememberFlags();
}

int CaptureService::consumeNotice()
{
    Lock lock(this, false);
    if (!lock.held())
        return 0;
    const int notice = session_.consume_notice();
    rememberFlags();
    return notice;
}

bool CaptureService::pushPcm(const uint8_t *pcm, size_t length)
{
    // The microphone task still appends to SD while holding this lock. R8 moves that write off the lock.
    Lock lock(this);
    if (!lock.held())
        return false;
    return session_.push_pcm(pcm, length);
}

void CaptureService::notifyMicFailed()
{
    Lock lock(this);
    if (!lock.held())
        return;
    session_.notify_mic_failed();
}

bool CaptureService::routeTouch(int16_t x, int16_t y, uint8_t phase)
{
    Lock lock(this, false);
    if (!lock.held()) {
        if (touch_count_ >= 32)
            return false;
        const int tail = (touch_head_ + touch_count_) % 32;
        touch_queue_[tail] = {x, y, phase};
        ++touch_count_;
        return true;
    }
    drainTouches();
    const bool accepted = session_.route_touch(x, y, phase, 0);
    rememberFlags();
    return accepted;
}

void CaptureService::render()
{
    Lock lock(this, false);
    if (!lock.held())
        return;
    session_.render_update();
    rememberFlags();
}

bool CaptureService::finish()
{
    Lock lock(this);
    if (!lock.held())
        return false;
    const bool stopped = session_.stop_recording();
    if (stopped)
        session_.release_capture();
    rememberFlags();
    return stopped;
}

void CaptureService::cancel()
{
    Lock lock(this);
    if (!lock.held())
        return;
    session_.cancel_recording();
    session_.release_capture();
    rememberFlags();
}

void CaptureService::discard()
{
    Lock lock(this);
    if (!lock.held())
        return;
    session_.storage().discard_in_progress();
    session_.release_capture();
    rememberFlags();
}

void CaptureService::beginReverse(int64_t now)
{
    Lock lock(this);
    if (!lock.held())
        return;
    session_.begin_reverse(now);
    rememberFlags();
}

void CaptureService::beginCollapse(int64_t now)
{
    Lock lock(this);
    if (!lock.held())
        return;
    session_.begin_finish_collapse(now);
    rememberFlags();
}

bool CaptureService::inputOpen() const
{
    Lock lock(this, false);
    if (!lock.held())
        return cached_input_open_;
    const_cast<CaptureService *>(this)->rememberFlags();
    return cached_input_open_;
}

bool CaptureService::wiping() const
{
    Lock lock(this, false);
    if (!lock.held())
        return cached_wiping_;
    const_cast<CaptureService *>(this)->rememberFlags();
    return cached_wiping_;
}
bool CaptureService::recording() const
{
    Lock lock(this, false);
    if (!lock.held())
        return cached_recording_;
    const_cast<CaptureService *>(this)->rememberFlags();
    return cached_recording_;
}
bool CaptureService::reversing() const
{
    Lock lock(this, false);
    if (!lock.held())
        return cached_reversing_;
    const_cast<CaptureService *>(this)->rememberFlags();
    return cached_reversing_;
}
int32_t CaptureService::ringRadius() const
{
    return session_.ring_outer_radius();
}
uint8_t CaptureService::selectedColor() const
{
    return session_.selected_color();
}

bool CaptureService::blocksInboxFetch()
{
    Lock lock(this, false);
    if (!lock.held())
        return cached_blocks_;
    rememberFlags();
    return cached_blocks_;
}

void CaptureService::attachSurface(uint16_t *pixels, lv_obj_t *canvas)
{
    auto *renderer = static_cast<lab10_render_t *>(session_.renderer_handle());
    s_paint = this;
    s_canvas = canvas;
    if (!renderer)
        return;
    lab10_render_hooks_t hooks{};
    hooks.on_publish_pixel = onPixel;
    hooks.on_invalidation = onInvalidation;
    hooks.context = pixels;
    (void)lab10_render_set_hooks(renderer, &hooks);
}

void CaptureService::detachSurface()
{
    auto *renderer = static_cast<lab10_render_t *>(session_.renderer_handle());
    if (renderer) {
        lab10_render_hooks_t hooks{};
        (void)lab10_render_set_hooks(renderer, &hooks);
    }
    if (s_paint == this)
        s_paint = nullptr;
    s_canvas = nullptr;
}

bool CaptureService::finishedInfo(const char *clientMessageId, uint32_t *audioBytes, uint32_t *sketchBytes)
{
    Lock lock(this);
    if (!lock.held() || !clientMessageId)
        return false;
    for (int i = 0; i < fl_product_record::kAttachedFinishedSlots; ++i) {
        const auto *slot = session_.storage().finished_slot(i);
        if (!slot || std::strcmp(slot->client_message_id, clientMessageId) != 0)
            continue;
        if (audioBytes)
            *audioBytes = slot->audio_bytes;
        if (sketchBytes)
            *sketchBytes = slot->sketch_bytes;
        return true;
    }
    return false;
}

bool CaptureService::readFinished(const char *clientMessageId, int kind, uint32_t offset, uint8_t *dest,
                                  uint32_t length, uint32_t *read)
{
    Lock lock(this);
    if (!lock.held() || !read)
        return false;
    *read = 0;
    fl_product_record::MediaKind media;
    if (kind == 2)
        media = fl_product_record::kMediaSketch;
    else if (kind == 1)
        media = fl_product_record::kMediaAudio;
    else
        return false;
    size_t n = 0;
    if (!session_.storage().read_finished(clientMessageId, media, offset, dest, length, &n))
        return false;
    *read = static_cast<uint32_t>(n);
    return true;
}

bool CaptureService::markUploading(const char *clientMessageId)
{
    Lock lock(this);
    if (!lock.held())
        return false;
    return session_.storage().mark_finished_uploading(clientMessageId);
}

void CaptureService::clearUpload(const char *clientMessageId)
{
    Lock lock(this);
    if (!lock.held())
        return;
    session_.storage().clear_finished_upload(clientMessageId);
}

bool CaptureService::identityFor(const char *clientMessageId, fl_product_record::SendIdentity *identity)
{
    Lock lock(this);
    if (!lock.held())
        return false;
    return session_.storage().identity_for(clientMessageId, identity);
}

const fl_product_record::StoredObject *CaptureService::finishedSlot(int index)
{
    return session_.storage().finished_slot(index);
}

} // namespace product_ui
