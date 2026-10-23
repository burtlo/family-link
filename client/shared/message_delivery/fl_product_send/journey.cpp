#include "fl_product_send_journey.hpp"

#include <string.h>

namespace fl_product_send {

SendJourney::SendJourney() { reset(); }

void SendJourney::reset()
{
    memset(this, 0, sizeof(*this));
    outbox_.set_wifi(1);
}

void SendJourney::remember_inbox_selection(const char *message_id)
{
    if (message_id == 0) {
        inbox_snapshot_[0] = '\0';
        return;
    }
    strncpy(inbox_snapshot_, message_id, sizeof(inbox_snapshot_) - 1);
}

const char *SendJourney::restored_inbox_selection() const { return restored_selection_; }

void SendJourney::set_session_generation(uint32_t generation)
{
    session_generation_ = generation;
    outbox_.set_worker_generation(generation);
}

void SendJourney::set_wifi(int up) { outbox_.set_wifi(up); }

void SendJourney::set_refresh_failed(int failed) { outbox_.set_refresh_failed(failed); }

void SendJourney::on_mute_during_capture()
{
    mute_discarded_ = 1;
    mute_held_ = 0;
    outbox_.reset();
    picker_.reset();
}

void SendJourney::on_mute_after_finish(int held)
{
    mute_held_ = held ? 1 : 0;
    outbox_.set_upload_blocked(mute_held_);
}

void SendJourney::on_same_user_signed_in()
{
    if (!mute_held_) {
        return;
    }
    mute_held_ = 0;
    outbox_.set_upload_blocked(0);
}

int SendJourney::mute_discarded_in_progress() const { return mute_discarded_; }

int SendJourney::mute_held_finished() const { return mute_held_; }

RecipientPicker &SendJourney::picker() { return picker_; }

SendOutbox &SendJourney::outbox() { return outbox_; }

int SendJourney::bind_test_payload(const FrozenPayload &payload, const uint8_t *audio,
                                   const uint8_t *sketch)
{
    return outbox_.bind_payload(payload, audio, sketch);
}

int SendJourney::auto_submit_when_ready(int64_t now_ms)
{
    if (picker_.selected_count() <= 0 || outbox_.client_message_id()[0] == '\0') {
        return 0;
    }
    outbox_.begin_submit(now_ms);
    strncpy(restored_selection_, inbox_snapshot_, sizeof(restored_selection_) - 1);
    return 1;
}

void SendJourney::advance_to(int64_t now_ms)
{
    outbox_.advance_to(now_ms);
    if (outbox_.ui_phase() == kSendUiHidden && outbox_.terminal() == kSendTerminalAccepted) {
        strncpy(restored_selection_, inbox_snapshot_, sizeof(restored_selection_) - 1);
    }
}

int SendJourney::inbox_fetch_may_proceed() const
{
    /* EXP-046 / DEC-051: inbox fetch continues during an active upload stream. */
    return outbox_.inbox_fetch_may_proceed();
}

int SendJourney::streaming_active() const { return outbox_.active(); }

SendUiPhase SendJourney::ui_phase() const { return outbox_.ui_phase(); }

} /* namespace fl_product_send */
