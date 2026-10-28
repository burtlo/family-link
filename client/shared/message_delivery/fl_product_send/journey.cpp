#include "fl_product_send_journey.hpp"

#include <stdio.h>
#include <string.h>

namespace fl_product_send
{
namespace
{

enum { kReceiptNameLimit = 25 };

void append_text(char *out, int cap, const char *src)
{
    int used;
    int i;
    if (out == 0 || cap <= 0 || src == 0) {
        return;
    }
    used = (int)strlen(out);
    if (used >= cap - 1) {
        return;
    }
    for (i = 0; src[i] != '\0' && used + 1 < cap; ++i) {
        out[used] = src[i];
        used += 1;
    }
    out[used] = '\0';
}

void first_token(const char *name, char *out, int cap)
{
    int i = 0;
    int n = 0;
    if (out == 0 || cap <= 0) {
        return;
    }
    out[0] = '\0';
    if (name == 0) {
        return;
    }
    while (name[i] == ' ' || name[i] == '\t') {
        i += 1;
    }
    while (name[i] != '\0' && name[i] != ' ' && name[i] != '\t' && n + 1 < cap) {
        out[n] = name[i];
        n += 1;
        i += 1;
    }
    out[n] = '\0';
}

void format_name_line(const char names[][kMaxName], int count, char *out, int cap)
{
    char full[384];
    char first[kMaxName];
    int i;
    if (out == 0 || cap <= 0) {
        return;
    }
    out[0] = '\0';
    if (names == 0 || count <= 0) {
        return;
    }
    full[0] = '\0';
    for (i = 0; i < count; ++i) {
        if (i > 0) {
            append_text(full, (int)sizeof(full), ", ");
        }
        append_text(full, (int)sizeof(full), names[i]);
    }
    if ((int)strlen(full) > kReceiptNameLimit) {
        int others = count - 1;
        first_token(names[0], first, (int)sizeof(first));
        if (others > 0) {
            snprintf(full, sizeof(full), "%s +%d", first, others);
        } else {
            snprintf(full, sizeof(full), "%s", first);
        }
    }
    append_text(out, cap, full);
}

void write_capture_uuid(char *out, int cap, uint32_t seq, uint32_t seed)
{
    uint32_t a = 0xA1000000u ^ (seq * 0x9E3779B9u) ^ seed;
    uint32_t b = 0xB2000000u ^ (seq * 0x85EBCA6Bu) ^ ((seed << 11) | (seed >> 21));
    uint32_t c = 0xC3000000u ^ (seq * 0xC2B2AE35u) ^ (seed * 0x27D4EB2Fu);
    if (out == 0 || cap <= 0) {
        return;
    }
    snprintf(out, (size_t)cap, "%08x-%04x-%04x-%04x-%08x%04x", (unsigned)a,
             (unsigned)((b >> 16) & 0xffffu), (unsigned)(0x4000u | (b & 0x0fffu)),
             (unsigned)(0x8000u | ((c >> 16) & 0x0fffu)), (unsigned)c, (unsigned)(seq & 0xffffu));
}

} /* namespace */

SendJourney::SendJourney()
{
    reset();
}

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

const char *SendJourney::remembered_inbox_selection() const
{
    return inbox_snapshot_;
}

const char *SendJourney::restored_inbox_selection() const
{
    return restored_selection_;
}

void SendJourney::set_session_generation(uint32_t generation)
{
    session_generation_ = generation;
    outbox_.set_worker_generation(generation);
}

void SendJourney::set_wifi(int up)
{
    outbox_.set_wifi(up);
}

void SendJourney::set_refresh_failed(int failed)
{
    outbox_.set_refresh_failed(failed);
}

void SendJourney::on_mute_during_capture()
{
    mute_discarded_ = 1;
    mute_held_ = 0;
    outbox_.reset();
    picker_.reset();
    clear_capture_freeze();
}

void SendJourney::seed_capture_ids(uint32_t seed)
{
    capture_id_seed_ = seed;
}

void SendJourney::freeze_capture_recipients()
{
    frozen_recipient_count_ =
        picker_.copy_selection(frozen_recipient_ids_, frozen_names_, kMaxRecipients);
    if (frozen_recipient_count_ <= 0) {
        capture_client_message_id_[0] = '\0';
        return;
    }
    capture_id_seq_ += 1u;
    write_capture_uuid(capture_client_message_id_, (int)sizeof(capture_client_message_id_), capture_id_seq_,
                       capture_id_seed_);
}

void SendJourney::clear_capture_freeze()
{
    memset(frozen_recipient_ids_, 0, sizeof(frozen_recipient_ids_));
    memset(frozen_names_, 0, sizeof(frozen_names_));
    frozen_recipient_count_ = 0;
    capture_client_message_id_[0] = '\0';
}

int SendJourney::frozen_recipient_count() const
{
    return frozen_recipient_count_;
}

const char *SendJourney::frozen_recipient_id(int index) const
{
    if (index < 0 || index >= frozen_recipient_count_) {
        return "";
    }
    return frozen_recipient_ids_[index];
}

int SendJourney::copy_frozen_names(char names[][kMaxName], int cap) const
{
    int n;
    int i;
    if (names == 0 || cap <= 0) {
        return 0;
    }
    n = frozen_recipient_count_;
    if (n > cap) {
        n = cap;
    }
    for (i = 0; i < n; ++i) {
        strncpy(names[i], frozen_names_[i], kMaxName - 1);
        names[i][kMaxName - 1] = '\0';
    }
    return n;
}

void SendJourney::capture_receipt_names()
{
    receipt_name_count_ = copy_frozen_names(receipt_names_, kMaxRecipients);
}

void SendJourney::install_receipt_names(const char names[][kMaxName], int count)
{
    int i;
    memset(receipt_names_, 0, sizeof(receipt_names_));
    receipt_name_count_ = 0;
    if (names == 0 || count <= 0) {
        return;
    }
    if (count > kMaxRecipients) {
        count = kMaxRecipients;
    }
    for (i = 0; i < count; ++i) {
        strncpy(receipt_names_[i], names[i], kMaxName - 1);
        receipt_names_[i][kMaxName - 1] = '\0';
    }
    receipt_name_count_ = count;
}

void SendJourney::receipt_title(char *out, int cap) const
{
    if (out == 0 || cap <= 0) {
        return;
    }
    snprintf(out, (size_t)cap, "sending to ...");
}

void SendJourney::receipt_subtitle(char *out, int cap) const
{
    if (receipt_name_count_ > 0) {
        format_name_line(receipt_names_, receipt_name_count_, out, cap);
        return;
    }
    format_name_line(frozen_names_, frozen_recipient_count_, out, cap);
}

const char *SendJourney::capture_client_message_id() const
{
    return capture_client_message_id_;
}

void SendJourney::on_mute_after_finish(int held)
{
    mute_held_ = held ? 1 : 0;
    outbox_.set_upload_blocked(mute_held_);
    if (mute_held_) {
        outbox_.set_signed_in_user(0);
    }
}

void SendJourney::set_signed_in_user(const char *user_id)
{
    outbox_.set_signed_in_user(user_id);
}

void SendJourney::on_same_user_signed_in(const char *user_id)
{
    outbox_.set_signed_in_user(user_id);
    if (!mute_held_) {
        return;
    }
    if (!outbox_.signed_in_matches_sender()) {
        return;
    }
    mute_held_ = 0;
    outbox_.set_upload_blocked(0);
}

void SendJourney::leave_wait()
{
    outbox_.leave_wait();
}

const char *SendJourney::body_label() const
{
    return outbox_.body_label();
}

int SendJourney::mute_discarded_in_progress() const
{
    return mute_discarded_;
}

int SendJourney::mute_held_finished() const
{
    return mute_held_;
}

RecipientPicker &SendJourney::picker()
{
    return picker_;
}

SendOutbox &SendJourney::outbox()
{
    return outbox_;
}

int SendJourney::bind_test_payload(const FrozenPayload &payload, const uint8_t *audio,
                                   const uint8_t *sketch)
{
    return outbox_.bind_payload(payload, audio, sketch);
}

int SendJourney::bind_stored_payload(const FrozenPayload &payload, const MediaPartReader &reader)
{
    return outbox_.bind_stored(payload, reader);
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

int SendJourney::streaming_active() const
{
    return outbox_.active();
}

SendUiPhase SendJourney::ui_phase() const
{
    return outbox_.ui_phase();
}

} /* namespace fl_product_send */
