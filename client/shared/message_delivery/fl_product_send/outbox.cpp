#include "fl_product_send_outbox.hpp"

#include "fl_product_send_classify.hpp"
#include "fl_product_send_p1_wire.hpp"
#include "fl_product_send_sha256.hpp"

#include <cinttypes>
#include <stdio.h>
#include <string.h>

namespace fl_product_send {
namespace {

int hex_digit(char c)
{
    if (c >= '0' && c <= '9') {
        return 1;
    }
    if (c >= 'a' && c <= 'f') {
        return 1;
    }
    if (c >= 'A' && c <= 'F') {
        return 1;
    }
    return 0;
}

int client_message_id_is_uuid(const char *id)
{
    static const int groups[5] = {8, 4, 4, 4, 12};
    int index = 0;
    int group;
    if (id == 0) {
        return 0;
    }
    for (group = 0; group < 5; ++group) {
        int count;
        if (group > 0) {
            if (id[index] != '-') {
                return 0;
            }
            index += 1;
        }
        for (count = 0; count < groups[group]; ++count) {
            if (!hex_digit(id[index])) {
                return 0;
            }
            index += 1;
        }
    }
    return id[index] == '\0';
}

} /* namespace */

SendOutbox::SendOutbox() { reset(); }

void SendOutbox::reset()
{
    memset(this, 0, sizeof(*this));
    wifi_up_ = 1;
    step_ = kWireIdle;
    ui_ = kSendUiHidden;
}

void SendOutbox::set_wifi(int up) { wifi_up_ = up ? 1 : 0; }

void SendOutbox::set_refresh_failed(int failed) { refresh_failed_ = failed ? 1 : 0; }

void SendOutbox::set_upload_blocked(int blocked) { upload_blocked_ = blocked ? 1 : 0; }

void SendOutbox::set_worker_generation(uint32_t generation) { worker_generation_ = generation; }

void SendOutbox::set_signed_in_user(const char *user_id)
{
    memset(signed_in_user_id_, 0, sizeof(signed_in_user_id_));
    if (user_id == 0 || user_id[0] == '\0') {
        return;
    }
    strncpy(signed_in_user_id_, user_id, sizeof(signed_in_user_id_) - 1);
}

int SendOutbox::signed_in_matches_sender() const
{
    if (signed_in_user_id_[0] == '\0' || payload_.sender_user_id[0] == '\0') {
        return 0;
    }
    return strcmp(signed_in_user_id_, payload_.sender_user_id) == 0 ? 1 : 0;
}

int SendOutbox::bind_payload(const FrozenPayload &payload, const uint8_t *audio, const uint8_t *sketch)
{
    uint8_t digest[32];
    if (!client_message_id_is_uuid(payload.client_message_id) || payload.recipient_count <= 0 || audio == 0) {
        return 0;
    }
    payload_ = payload;
    audio_ = audio;
    sketch_ = sketch;
    reader_.ctx = 0;
    reader_.read_at = 0;
    audio_parts_total_ = (uint32_t)part_count_for_bytes(payload.audio_byte_count);
    sketch_parts_total_ =
        payload.sketch_present ? (uint32_t)part_count_for_bytes(payload.sketch_byte_count) : 0u;
    sha256_bytes(audio, payload.audio_byte_count, digest);
    sha256_hex(digest, audio_sha_);
    if (payload.sketch_present && sketch != 0 && payload.sketch_byte_count > 0) {
        sha256_bytes(sketch, payload.sketch_byte_count, digest);
        sha256_hex(digest, sketch_sha_);
    } else {
        sketch_sha_[0] = '\0';
    }
    return 1;
}

int SendOutbox::hash_stored(int kind, uint32_t total, char *hex_out)
{
    Sha256Stream stream;
    uint32_t offset = 0;
    uint8_t digest[32];
    if (hex_out == 0 || reader_.read_at == 0 || total == 0) {
        return 0;
    }
    sha256_stream_init(&stream);
    while (offset < total) {
        uint32_t want = total - offset;
        uint32_t got = 0;
        if (want > kPartSizeBytes) {
            want = kPartSizeBytes;
        }
        if (!reader_.read_at(reader_.ctx, kind, offset, part_window_, want, &got) || got != want) {
            return 0;
        }
        sha256_stream_update(&stream, part_window_, got);
        offset += got;
    }
    sha256_stream_final(&stream, digest);
    sha256_hex(digest, hex_out);
    return 1;
}

int SendOutbox::bind_stored(const FrozenPayload &payload, const MediaPartReader &reader)
{
    if (!client_message_id_is_uuid(payload.client_message_id) || payload.recipient_count <= 0 ||
        payload.audio_byte_count == 0 || reader.read_at == 0) {
        return 0;
    }
    payload_ = payload;
    audio_ = 0;
    sketch_ = 0;
    reader_ = reader;
    audio_parts_total_ = (uint32_t)part_count_for_bytes(payload.audio_byte_count);
    sketch_parts_total_ =
        payload.sketch_present ? (uint32_t)part_count_for_bytes(payload.sketch_byte_count) : 0u;
    if (!hash_stored(1, payload.audio_byte_count, audio_sha_)) {
        payload_.client_message_id[0] = '\0';
        reader_.read_at = 0;
        return 0;
    }
    if (payload.sketch_present && payload.sketch_byte_count > 0) {
        if (!hash_stored(2, payload.sketch_byte_count, sketch_sha_)) {
            payload_.client_message_id[0] = '\0';
            reader_.read_at = 0;
            return 0;
        }
    } else {
        sketch_sha_[0] = '\0';
        sketch_parts_total_ = 0;
    }
    return 1;
}

int SendOutbox::fill_part_body(HttpAction *action, int kind, const uint8_t *fallback, uint32_t offset,
                               uint32_t part_len)
{
    uint32_t got = 0;
    if (action == 0 || part_len == 0 || part_len > kPartSizeBytes) {
        return 0;
    }
    if (reader_.read_at != 0) {
        if (!reader_.read_at(reader_.ctx, kind, offset, part_window_, part_len, &got) || got != part_len) {
            return 0;
        }
        action->body = part_window_;
        action->body_len = part_len;
        return 1;
    }
    if (fallback == 0) {
        return 0;
    }
    action->body = fallback + offset;
    action->body_len = part_len;
    return 1;
}

void SendOutbox::begin_submit(int64_t now_ms)
{
    if (payload_.client_message_id[0] == '\0') {
        return;
    }
    terminal_ = kSendTerminalNone;
    left_wait_ = 0;
    complete_response_lost_ = 0;
    audio_parts_sent_ = 0;
    sketch_parts_sent_ = 0;
    server_message_id_[0] = '\0';
    step_ = kWireCreate;
    set_ui(kSendUiPreparing);
    next_action_ms_ = now_ms;
    part_immediate_retry_left_ = 1;
    part_retry_tier_ = 0;
}

void SendOutbox::request_cancel()
{
    /* A finished send is already in flight. Boot does not cancel that upload. */
}

void SendOutbox::leave_wait() { left_wait_ = 1; }

int SendOutbox::left_wait() const { return left_wait_; }

void SendOutbox::set_ui(SendUiPhase phase)
{
    ui_ = phase;
    if (phase == kSendUiSending || phase == kSendUiQueuedWait) {
        /* stay on wait copy */
    }
}

void SendOutbox::finish_terminal(SendTerminal terminal, SendUiPhase ui, int64_t now_ms)
{
    terminal_ = terminal;
    step_ = kWireIdle;
    set_ui(ui);
    if (ui == kSendUiSent || ui == kSendUiCouldntSend) {
        hold_until_ms_ = now_ms + kSentHoldMs;
    }
}

void SendOutbox::note_accepted(int64_t now_ms)
{
    /* Signed out, or she already left this wait: do not flash sent. */
    if (left_wait_ || upload_blocked_) {
        finish_terminal(kSendTerminalAccepted, kSendUiHidden, now_ms);
        return;
    }
    finish_terminal(kSendTerminalAccepted, kSendUiSent, now_ms);
}

void SendOutbox::apply_com013(Com013Class com, int64_t now_ms)
{
    if (com == kCom013Discard) {
        finish_terminal(kSendTerminalDiscarded, kSendUiCouldntSend, now_ms);
        return;
    }
    schedule_part_retry(now_ms);
}

void SendOutbox::schedule_part_retry(int64_t now_ms)
{
    int delay = kPartRetry30Ms;
    if (part_immediate_retry_left_ > 0) {
        part_immediate_retry_left_ -= 1;
        delay = kPartRetryImmediate;
    } else if (part_retry_tier_ == 0) {
        part_retry_tier_ = 1;
        delay = kPartRetry1Ms;
    } else if (part_retry_tier_ == 1) {
        part_retry_tier_ = 2;
        delay = kPartRetry2Ms;
    } else if (part_retry_tier_ == 2) {
        part_retry_tier_ = 3;
        delay = kPartRetry4Ms;
    }
    next_action_ms_ = now_ms + delay;
    set_ui(kSendUiQueuedWait);
}

int SendOutbox::active() const
{
    return step_ != kWireIdle || ui_ == kSendUiSent || ui_ == kSendUiCouldntSend;
}

SendUiPhase SendOutbox::ui_phase() const { return ui_; }

const char *SendOutbox::body_label() const
{
    if (ui_ == kSendUiQueuedWait) {
        return connectivity_wait_label(wifi_up_, refresh_failed_);
    }
    return ui_phase_label(ui_);
}

SendTerminal SendOutbox::terminal() const { return terminal_; }

const char *SendOutbox::server_message_id() const { return server_message_id_; }

const char *SendOutbox::client_message_id() const { return payload_.client_message_id; }

int SendOutbox::inbox_fetch_may_proceed() const
{
    /* DEC-051: carousel inbox refresh is not held for an active finished-message upload stream. */
    if (step_ != kWireIdle || ui_ == kSendUiSent) {
        return 1;
    }
    return 1;
}

int SendOutbox::part_size_bytes() const { return kPartSizeBytes; }

int SendOutbox::has_pending_http() const
{
    if (step_ == kWireIdle || ui_ == kSendUiSent) {
        return 0;
    }
    return 1;
}

void SendOutbox::advance_to(int64_t now_ms)
{
    if (now_ms < hold_until_ms_) {
        return;
    }
    if (ui_ == kSendUiSent) {
        finish_terminal(kSendTerminalAccepted, kSendUiHidden, now_ms);
        return;
    }
    if (ui_ == kSendUiCouldntSend) {
        finish_terminal(kSendTerminalDiscarded, kSendUiHidden, now_ms);
    }
}

int SendOutbox::part_bytes_for(uint32_t offset, uint32_t total, uint32_t *length_out) const
{
    uint32_t remain;
    if (length_out == 0) {
        return 0;
    }
    if (offset >= total) {
        return 0;
    }
    remain = total - offset;
    *length_out = remain > kPartSizeBytes ? kPartSizeBytes : (uint32_t)remain;
    return 1;
}

int SendOutbox::build_part_path(char *path, int cap, const char *kind, uint32_t sequence) const
{
    if (server_message_id_[0] == '\0') {
        return 0;
    }
    return snprintf(path, (size_t)cap, "/v2/outgoing/%s/parts/%s/%" PRIu32, server_message_id_, kind,
                    sequence) <
           cap;
}

int SendOutbox::prepare_http_action(HttpAction *action, int64_t now_ms)
{
    uint32_t part_len = 0;
    uint32_t offset = 0;
    if (action == 0 || step_ == kWireIdle || http_inflight_) {
        return 0;
    }
    if (now_ms < next_action_ms_) {
        return 0;
    }
    if (upload_blocked_) {
        set_ui(kSendUiQueuedWait);
        return 0;
    }
    if (!signed_in_matches_sender()) {
        set_ui(kSendUiQueuedWait);
        return 0;
    }
    if (!wifi_up_ || refresh_failed_) {
        set_ui(kSendUiQueuedWait);
        return 0;
    }
    memset(action, 0, sizeof(*action));
    if (step_ == kWireCreate) {
        int len = build_message_create(payload_, json_body_, (int)sizeof(json_body_));
        if (len <= 0) {
            return 0;
        }
        snprintf(action->method, sizeof(action->method), "POST");
        snprintf(action->path, sizeof(action->path), "/v2/outgoing");
        action->body = (const uint8_t *)json_body_;
        action->body_len = (uint32_t)len;
        action->is_json = 1;
        return 1;
    }
    if (step_ == kWireAudioPart) {
        offset = audio_parts_sent_ * kPartSizeBytes;
        if (!part_bytes_for(offset, payload_.audio_byte_count, &part_len)) {
            step_ = payload_.sketch_present ? kWireSketchPart : kWireComplete;
            return prepare_http_action(action, now_ms);
        }
        if (!build_part_path(action->path, (int)sizeof(action->path), "audio", audio_parts_sent_)) {
            return 0;
        }
        if (!fill_part_body(action, 1, audio_, offset, part_len)) {
            return 0;
        }
        snprintf(action->method, sizeof(action->method), "PUT");
        set_ui(kSendUiSending);
        return 1;
    }
    if (step_ == kWireSketchPart) {
        offset = sketch_parts_sent_ * kPartSizeBytes;
        if (!part_bytes_for(offset, payload_.sketch_byte_count, &part_len)) {
            step_ = kWireComplete;
            return prepare_http_action(action, now_ms);
        }
        if (!build_part_path(action->path, (int)sizeof(action->path), "sketch", sketch_parts_sent_)) {
            return 0;
        }
        if (!fill_part_body(action, 2, sketch_, offset, part_len)) {
            return 0;
        }
        snprintf(action->method, sizeof(action->method), "PUT");
        set_ui(kSendUiSending);
        return 1;
    }
    if (step_ == kWireComplete) {
        int len = build_message_complete(payload_, server_message_id_, audio_parts_total_, sketch_parts_total_,
                                         audio_sha_, sketch_sha_[0] != '\0' ? sketch_sha_ : 0, json_body_,
                                         (int)sizeof(json_body_));
        if (len <= 0) {
            return 0;
        }
        snprintf(action->method, sizeof(action->method), "POST");
        snprintf(action->path, sizeof(action->path), "/v2/outgoing/%s/complete", server_message_id_);
        action->body = (const uint8_t *)json_body_;
        action->body_len = (uint32_t)len;
        action->is_json = 1;
        return 1;
    }
    if (step_ == kWireLookup) {
        snprintf(action->method, sizeof(action->method), "GET");
        snprintf(action->path, sizeof(action->path), "/v2/outgoing/by-client-id/%s",
                 payload_.client_message_id);
        return 1;
    }
    return 0;
}

void SendOutbox::note_http_sent(uint32_t *seq_out)
{
    http_seq_ += 1u;
    http_inflight_ = 1;
    if (seq_out != 0) {
        *seq_out = http_seq_;
    }
}

void SendOutbox::cancel_http_sent() { http_inflight_ = 0; }

int SendOutbox::http_inflight() const { return http_inflight_; }

int SendOutbox::apply_http_result(const char *client_message_id, uint32_t seq, const HttpAction &action,
                                  const SendTransportResult &result, int64_t now_ms)
{
    if (seq != http_seq_ || http_inflight_ == 0) {
        return 0;
    }
    http_inflight_ = 0;
    if (client_message_id == 0 || strcmp(client_message_id, payload_.client_message_id) != 0) {
        return 0;
    }
    return apply_transport_result(action, result, now_ms);
}

int SendOutbox::apply_transport_result(const HttpAction &action, const SendTransportResult &result,
                                       int64_t now_ms)
{
    WireParseResult parsed;
    (void)action;
    if (step_ == kWireIdle) {
        return 0;
    }
    if (!result.transport_ok || result.response_lost) {
        if (step_ == kWireComplete) {
            complete_response_lost_ = 1;
            step_ = kWireLookup;
            next_action_ms_ = now_ms;
            return 1;
        }
        apply_com013(classify_http_result(wifi_up_, 0, 0, 0, 0, refresh_failed_), now_ms);
        return 1;
    }
    if (step_ == kWireAudioPart || step_ == kWireSketchPart) {
        if (result.http_status == 200 && result.body_len >= 2 && result.body[0] == '{') {
            part_immediate_retry_left_ = 1;
            part_retry_tier_ = 0;
            if (step_ == kWireAudioPart) {
                audio_parts_sent_ += 1;
                if (audio_parts_sent_ >= audio_parts_total_) {
                    step_ = payload_.sketch_present ? kWireSketchPart : kWireComplete;
                }
            } else {
                sketch_parts_sent_ += 1;
                if (sketch_parts_sent_ >= sketch_parts_total_) {
                    step_ = kWireComplete;
                }
            }
            next_action_ms_ = now_ms;
            set_ui(kSendUiSending);
            return 1;
        }
        apply_com013(classify_http_result(wifi_up_, 1, result.http_status, result.body, result.body_len,
                                          refresh_failed_),
                     now_ms);
        return 1;
    }
    if (!parse_delivery_response(result.body, result.body_len, &parsed)) {
        apply_com013(classify_http_result(wifi_up_, 1, result.http_status, result.body, result.body_len,
                                          refresh_failed_),
                     now_ms);
        return 1;
    }
    if (strcmp(parsed.schema, "fl.send-receipt/2") == 0 && strcmp(parsed.state, "accepted") == 0) {
        note_accepted(now_ms);
        return 1;
    }
    if (strcmp(parsed.schema, "fl.send-open/2") == 0) {
        strncpy(server_message_id_, parsed.message_id, sizeof(server_message_id_) - 1);
        audio_parts_sent_ = parsed.audio_parts_ack;
        sketch_parts_sent_ = parsed.sketch_parts_ack;
        step_ = kWireAudioPart;
        next_action_ms_ = now_ms;
        set_ui(kSendUiSending);
        return 1;
    }
    if (result.http_status == 409) {
        finish_terminal(kSendTerminalDiscarded, kSendUiCouldntSend, now_ms);
        return 1;
    }
    schedule_part_retry(now_ms);
    return 1;
}

int SendOutbox::audio_part_sequence() const { return (int)audio_parts_sent_; }

int SendOutbox::sketch_part_sequence() const { return (int)sketch_parts_sent_; }

int SendOutbox::trace_line(char *out, int out_cap) const
{
    if (out == 0 || out_cap <= 0) {
        return 0;
    }
    return snprintf(out, (size_t)out_cap, "P8_SEND phase=%s step=%d client=%s server=%s audio_part=%u/%u",
                    ui_phase_label(ui_), (int)step_, payload_.client_message_id, server_message_id_,
                    (unsigned)audio_parts_sent_, (unsigned)audio_parts_total_);
}

} /* namespace fl_product_send */
