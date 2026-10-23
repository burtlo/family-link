#ifndef FL_PRODUCT_RECORD_SESSION_HPP
#define FL_PRODUCT_RECORD_SESSION_HPP

#include "fl_product_record_storage.hpp"
#include "fl_product_record_types.hpp"

#include <stdint.h>

namespace fl_product_record {

class RecordSession {
public:
    RecordSession();
    ~RecordSession();

    void boot(int64_t now_ms);
    void advance_to(int64_t now_ms);

    bool capture_active() const;
    bool ink_overflow() const;
    MicState mic_state() const;
    FinishReason last_finish() const;

    bool try_start(const char *client_message_id, MicState mic_at_start);
    void notify_mic_failed();
    bool route_touch(int16_t x, int16_t y, uint8_t phase, uint8_t touch_id);
    void poll_capture();
    bool stop_recording();
    void cancel_recording();

    AttachedStorage &storage();

    void *capture_handle();
    void *renderer_handle();
    uint64_t capture_clock_us() const;

private:
    bool finish_capture(FinishReason reason);
    void reset_runtime();

    AttachedStorage storage_;
    void *capture_;
    void *renderer_;
    uint64_t session_id_;
    int64_t start_ms_;
    int64_t now_ms_;
    bool recording_;
    bool ink_overflow_;
    MicState mic_;
    FinishReason last_finish_;
    uint64_t clock_us_;
};

} /* namespace fl_product_record */

#endif
