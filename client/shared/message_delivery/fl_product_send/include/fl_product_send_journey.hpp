#ifndef FL_PRODUCT_SEND_JOURNEY_HPP
#define FL_PRODUCT_SEND_JOURNEY_HPP

#include "fl_product_send_outbox.hpp"
#include "fl_product_send_picker.hpp"
#include "fl_product_send_types.hpp"

namespace fl_product_send {

class SendJourney {
public:
    SendJourney();

    void reset();
    void remember_inbox_selection(const char *message_id);
    const char *restored_inbox_selection() const;
    void set_session_generation(uint32_t generation);
    void set_wifi(int up);
    void set_refresh_failed(int failed);
    void on_mute_during_capture();
    void on_mute_after_finish(int held);
    void on_same_user_signed_in();
    int mute_discarded_in_progress() const;
    int mute_held_finished() const;

    RecipientPicker &picker();
    SendOutbox &outbox();
    int bind_test_payload(const FrozenPayload &payload, const uint8_t *audio, const uint8_t *sketch);
    int auto_submit_when_ready(int64_t now_ms);
    void advance_to(int64_t now_ms);
    int inbox_fetch_may_proceed() const;
    int streaming_active() const;
    SendUiPhase ui_phase() const;

private:
    char inbox_snapshot_[kMaxInboxSelectionId];
    char restored_selection_[kMaxInboxSelectionId];
    RecipientPicker picker_;
    SendOutbox outbox_;
    int mute_discarded_;
    int mute_held_;
    uint32_t session_generation_;
};

} /* namespace fl_product_send */

#endif
