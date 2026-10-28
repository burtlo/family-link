#ifndef FL_PRODUCT_RECORD_SESSION_HPP
#define FL_PRODUCT_RECORD_SESSION_HPP

#include "fl_product_record_storage.hpp"
#include "fl_product_record_types.hpp"

#include <stddef.h>
#include <stdint.h>

namespace fl_product_record
{

enum SessionNotice {
    kSessionNoticeNone = 0,
    kSessionNoticeWipeCompleted = 1,
    kSessionNoticeTakeFinished = 2,
    kSessionNoticeCaptureFailed = 3
};

class RecordSession
{
  public:
    RecordSession();
    ~RecordSession();

    void boot(int64_t now_ms);
    void advance_to(int64_t now_ms);
    int consume_notice();

    bool capture_active() const;
    bool wiping() const;
    bool reversing() const;
    bool ink_overflow() const;
    MicState mic_state() const;
    FinishReason last_finish() const;
    int64_t now_ms() const;
    /* Outer radius of the red ring. Ease-in-out quint from the lab origin. */
    int32_t ring_outer_radius() const;

    bool try_start(const char *client_message_id, MicState mic_at_start);
    void begin_reverse(int64_t now_ms);
    /* Collapse the full canvas after a committed take. Does not discard storage. */
    void begin_finish_collapse(int64_t now_ms);
    /* Host-testable PCM feed. Chunks during the wipe are dropped. */
    bool push_pcm(const uint8_t *pcm, size_t len);
    void notify_mic_failed();
    bool route_touch(int16_t x, int16_t y, uint8_t phase, uint8_t touch_id);
    void poll_capture();
    /* Drives the hooks installed on renderer_handle(). UI task only. */
    void render_update();
    bool render_backlog(size_t *source_points, size_t *pending_points, size_t *drawn_last) const;
    uint8_t selected_color() const;
    bool stop_recording();
    void cancel_recording();

    void attach_media(MediaBackend *backend);
    AttachedStorage &storage();

    void *capture_handle();
    void *renderer_handle();
    uint64_t capture_clock_us() const;

  private:
    bool finish_capture(FinishReason reason);
    bool open_kept_capture();
    void reset_runtime();
    int64_t ring_elapsed_ms(int64_t origin_ms) const;

    AttachedStorage storage_;
    void *capture_;
    void *renderer_;
    uint64_t session_id_;
    int64_t start_ms_;
    int64_t now_ms_;
    int64_t wipe_start_ms_;
    int64_t reverse_start_ms_;
    int32_t reverse_from_radius_;
    bool recording_;
    bool wiping_;
    bool reversing_;
    bool ink_overflow_;
    MicState mic_;
    FinishReason last_finish_;
    int notice_;
    uint64_t clock_us_;
};

} /* namespace fl_product_record */

#endif
