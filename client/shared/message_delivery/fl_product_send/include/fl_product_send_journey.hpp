#ifndef FL_PRODUCT_SEND_JOURNEY_HPP
#define FL_PRODUCT_SEND_JOURNEY_HPP

#include "fl_product_send_outbox.hpp"
#include "fl_product_send_picker.hpp"
#include "fl_product_send_types.hpp"

namespace fl_product_send
{

class SendJourney
{
  public:
    SendJourney();

    void reset();
    void remember_inbox_selection(const char *message_id);
    const char *remembered_inbox_selection() const;
    const char *restored_inbox_selection() const;
    void set_session_generation(uint32_t generation);
    void set_wifi(int up);
    void set_refresh_failed(int failed);
    void on_mute_during_capture();
    void on_mute_after_finish(int held);
    void set_signed_in_user(const char *user_id);
    void on_same_user_signed_in(const char *user_id);
    void leave_wait();
    /* Stored takes outlive a reboot, so each boot needs its own seed or
     * the first new take repeats an id still on the card. */
    void seed_capture_ids(uint32_t seed);
    /* Copy the current picker selection and a new UUID. Later roster
     * refreshes do not rewrite this set. */
    void freeze_capture_recipients();
    void clear_capture_freeze();
    int frozen_recipient_count() const;
    const char *frozen_recipient_id(int index) const;
    int copy_frozen_names(char names[][kMaxName], int cap) const;
    /* Snapshot the names that stay on the receipt while a later take freezes another set. */
    void capture_receipt_names();
    void install_receipt_names(const char names[][kMaxName], int count);
    void receipt_title(char *out, int cap) const;
    void receipt_subtitle(char *out, int cap) const;
    const char *capture_client_message_id() const;
    int mute_discarded_in_progress() const;
    int mute_held_finished() const;
    const char *body_label() const;

    RecipientPicker &picker();
    SendOutbox &outbox();
    int bind_test_payload(const FrozenPayload &payload, const uint8_t *audio, const uint8_t *sketch);
    int bind_stored_payload(const FrozenPayload &payload, const MediaPartReader &reader);
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
    char frozen_recipient_ids_[kMaxRecipients][kMaxUserId];
    char frozen_names_[kMaxRecipients][kMaxName];
    int frozen_recipient_count_;
    char receipt_names_[kMaxRecipients][kMaxName];
    int receipt_name_count_;
    char capture_client_message_id_[kMaxClientMessageId];
    uint32_t capture_id_seq_;
    uint32_t capture_id_seed_;
};

} /* namespace fl_product_send */

#endif
