#include "fl_product_inbox_wire.hpp"

#include <inttypes.h>
#include <stdio.h>
#include <string.h>

namespace fl_product_inbox
{
namespace
{

constexpr int kInboxTimeoutMs = 1000;
constexpr int kMediaTimeoutMs = 5000;
constexpr int kQueueRefuseBackoffMs = 1000;
constexpr int kLastViewedRetainMs = 60000;

void copy_text(char *dst, int cap, const char *src)
{
    int i = 0;
    if (dst == 0 || cap <= 0) {
        return;
    }
    if (src == 0) {
        dst[0] = 0;
        return;
    }
    for (; i < cap - 1 && src[i] != 0; ++i) {
        dst[i] = src[i];
    }
    dst[i] = 0;
}

int text_eq(const char *a, const char *b)
{
    if (a == 0 || b == 0) {
        return 0;
    }
    return strcmp(a, b) == 0 ? 1 : 0;
}

/* Same 8-4-4-4-12 shape as fl_product_login::format_p1_operation_id. */
void format_operation_uuid(uint64_t operation_id, char *out, int out_cap)
{
    if (out == 0 || out_cap < 37) {
        if (out != 0 && out_cap > 0) {
            out[0] = 0;
        }
        return;
    }
    snprintf(out, (size_t)out_cap, "00000000-0000-4000-8000-%012llx",
             (unsigned long long)(operation_id & 0xffffffffffffULL));
}

int message_id_safe(const char *message_id)
{
    int i;
    if (message_id == 0 || message_id[0] == 0) {
        return 0;
    }
    for (i = 0; message_id[i] != 0; ++i) {
        char c = message_id[i];
        if (c == '"' || c == '\\' || c < 0x20) {
            return 0;
        }
    }
    return 1;
}

void clear_job(InboxWireJob *job)
{
    if (job != 0) {
        memset(job, 0, sizeof(*job));
    }
}

int playback_active(const CarouselSession &session)
{
    if (session.connectivity_slot() != kSlotNone) {
        return 0;
    }
    if (session.playback_operation_id() == 0) {
        return 0;
    }
    if (session.playback_phase() != kPlaybackPlaying && session.playback_phase() != kPlaybackBuffering) {
        return 0;
    }
    return 1;
}

} /* namespace */

InboxWirePlanner::InboxWirePlanner()
{
    reset();
}

void InboxWirePlanner::reset()
{
    inbox_inflight_ = 0;
    last_viewed_inflight_ = 0;
    audio_inflight_ = 0;
    sketch_inflight_ = 0;
    took_inbox_arm_ = 0;
    inbox_quiet_until_ms_ = 0;
    last_viewed_quiet_until_ms_ = 0;
    audio_quiet_until_ms_ = 0;
    sketch_quiet_until_ms_ = 0;
    have_last_viewed_body_ = 0;
    next_last_viewed_op_ = 1;
    last_viewed_uuid_[0] = 0;
    last_viewed_message_[0] = 0;
    last_viewed_body_[0] = 0;
    last_viewed_body_len_ = 0;
    last_viewed_retain_until_ms_ = 0;
    last_viewed_acknowledged_[0] = 0;
    last_viewed_suppress_[0] = 0;
}

void InboxWirePlanner::fill_inbox_job(CarouselSession &session, InboxWireJob *job)
{
    clear_job(job);
    job->kind = kInboxWireInbox;
    copy_text(job->method, (int)sizeof(job->method), "GET");
    copy_text(job->path, (int)sizeof(job->path), "/v2/inbox");
    job->generation = session.session_generation();
    job->timeout_ms = kInboxTimeoutMs;
}

int InboxWirePlanner::fill_last_viewed_job(CarouselSession &session, InboxWireJob *job, int64_t now_ms)
{
    const char *pending = session.pending_last_viewed_message_id();
    int reuse;
    int wrote;
    clear_job(job);
    if (pending == 0 || pending[0] == 0 || !message_id_safe(pending)) {
        return 0;
    }
    if (text_eq(pending, last_viewed_acknowledged_) || text_eq(pending, last_viewed_suppress_)) {
        return 0;
    }
    reuse = have_last_viewed_body_ && text_eq(last_viewed_message_, pending) &&
            now_ms < last_viewed_retain_until_ms_ && last_viewed_body_len_ > 0;
    if (!reuse) {
        uint64_t op = next_last_viewed_op_;
        if (op == 0) {
            op = 1;
        }
        next_last_viewed_op_ = op + 1;
        if (next_last_viewed_op_ == 0) {
            next_last_viewed_op_ = 1;
        }
        format_operation_uuid(op, last_viewed_uuid_, (int)sizeof(last_viewed_uuid_));
        wrote = snprintf(last_viewed_body_, sizeof(last_viewed_body_),
                         "{\"schema\":\"fl.last-viewed-update/2\",\"operation_id\":\"%s\",\"message_id\":\"%s\"}",
                         last_viewed_uuid_, pending);
        if (wrote <= 0 || wrote >= (int)sizeof(last_viewed_body_)) {
            last_viewed_body_[0] = 0;
            last_viewed_body_len_ = 0;
            have_last_viewed_body_ = 0;
            return 0;
        }
        last_viewed_body_len_ = wrote;
        copy_text(last_viewed_message_, kMaxMessageId, pending);
        last_viewed_retain_until_ms_ = now_ms + kLastViewedRetainMs;
        have_last_viewed_body_ = 1;
    }
    job->kind = kInboxWireLastViewed;
    copy_text(job->method, (int)sizeof(job->method), "PUT");
    copy_text(job->path, (int)sizeof(job->path), "/v2/inbox/last-viewed");
    memcpy(job->body, last_viewed_body_, (size_t)last_viewed_body_len_);
    job->body[last_viewed_body_len_] = 0;
    job->body_len = last_viewed_body_len_;
    copy_text(job->operation_uuid, (int)sizeof(job->operation_uuid), last_viewed_uuid_);
    copy_text(job->message_id, kMaxMessageId, last_viewed_message_);
    job->generation = session.session_generation();
    job->timeout_ms = kInboxTimeoutMs;
    return 1;
}

int InboxWirePlanner::fill_media_job(CarouselSession &session, InboxWireJob *job, int sketch, int64_t now_ms)
{
    const InboxMessage *message;
    uint32_t stored;
    uint32_t total;
    int wrote;
    (void)now_ms;
    clear_job(job);
    if (!playback_active(session)) {
        return 0;
    }
    if (session.selected_index() < 0) {
        return 0;
    }
    message = session.message_at(session.selected_index());
    if (message == 0 || !message_id_safe(message->message_id)) {
        return 0;
    }
    if (!text_eq(message->message_id, session.selected_message_id())) {
        return 0;
    }
    if (sketch) {
        if (!message->sketch.present) {
            return 0;
        }
        stored = (uint32_t)session.sketch_bytes_stored();
        total = message->sketch.byte_count;
        if (total == 0 || stored >= total) {
            return 0;
        }
        wrote = snprintf(job->path, sizeof(job->path), "/v2/messages/%s/media/sketch", message->message_id);
        copy_text(job->sha256, kMaxSha256, message->sketch.sha256);
    } else {
        if (!message->audio.present) {
            return 0;
        }
        stored = (uint32_t)session.audio_bytes_buffered();
        total = message->audio.byte_count;
        if (total == 0 || stored >= total) {
            return 0;
        }
        wrote = snprintf(job->path, sizeof(job->path), "/v2/messages/%s/media/audio", message->message_id);
        copy_text(job->sha256, kMaxSha256, message->audio.sha256);
    }
    if (wrote <= 0 || wrote >= (int)sizeof(job->path)) {
        return 0;
    }
    snprintf(job->range_header, sizeof(job->range_header), "bytes=%" PRIu32 "-", stored);
    job->kind = sketch ? kInboxWireSketch : kInboxWireAudio;
    copy_text(job->method, (int)sizeof(job->method), "GET");
    copy_text(job->message_id, kMaxMessageId, message->message_id);
    job->generation = session.session_generation();
    job->operation_id = session.playback_operation_id();
    job->range_start = stored;
    job->asset_bytes = total;
    job->timeout_ms = kMediaTimeoutMs;
    return 1;
}

void InboxWirePlanner::collect(CarouselSession &session, InboxWireSlots *out, int64_t now_ms, int ordinary_due,
                               int recovery_due)
{
    int wifi_down;
    int connecting;
    int showing;
    int want_inbox;
    if (out == 0) {
        return;
    }
    memset(out, 0, sizeof(*out));
    wifi_down = session.connectivity_slot() == kSlotNoWifi;
    connecting = session.connectivity_slot() == kSlotConnecting;
    showing = session.ui_visible();
    if (session.pending_last_viewed_message_id()[0] == 0 && !last_viewed_inflight_) {
        last_viewed_suppress_[0] = 0;
        last_viewed_acknowledged_[0] = 0;
    }
    if (wifi_down) {
        return;
    }
    if (showing && session.recovery_pulse_pending() && connecting) {
        recovery_due = 1;
    }
    want_inbox = 0;
    if (showing) {
        if (session.inbox_fetch_armed()) {
            want_inbox = 1;
        } else if (connecting && recovery_due) {
            want_inbox = 1;
        } else if (!connecting && ordinary_due) {
            want_inbox = 1;
        }
    }
    if (want_inbox && !inbox_inflight_ && now_ms >= inbox_quiet_until_ms_) {
        fill_inbox_job(session, &out->inbox_job);
        out->inbox = 1;
        inbox_inflight_ = 1;
        took_inbox_arm_ = session.inbox_fetch_armed();
        session.consume_inbox_arm();
        session.clear_recovery_pulse();
    } else if (inbox_inflight_ && !wifi_down) {
        session.clear_recovery_pulse();
    }
    if (!last_viewed_inflight_ && now_ms >= last_viewed_quiet_until_ms_ &&
        session.last_viewed_write_due(now_ms) &&
        fill_last_viewed_job(session, &out->last_viewed_job, now_ms)) {
        out->last_viewed = 1;
        last_viewed_inflight_ = 1;
    }
    if (connecting) {
        return;
    }
    if (!audio_inflight_ && now_ms >= audio_quiet_until_ms_ &&
        fill_media_job(session, &out->audio_job, 0, now_ms)) {
        out->audio = 1;
        audio_inflight_ = 1;
    }
    if (!sketch_inflight_ && now_ms >= sketch_quiet_until_ms_ &&
        fill_media_job(session, &out->sketch_job, 1, now_ms)) {
        out->sketch = 1;
        sketch_inflight_ = 1;
    }
}

int InboxWirePlanner::media_job_cancelled(const CarouselSession &session, int kind)
{
    if (kind != kInboxWireAudio && kind != kInboxWireSketch) {
        return 0;
    }
    return session.media_fetch_allowed() == 0 ? 1 : 0;
}

void InboxWirePlanner::accept_queued(CarouselSession &session, int kind, int64_t now_ms)
{
    if (kind == kInboxWireInbox) {
        session.acknowledge_inbox_fetch(now_ms);
        took_inbox_arm_ = 0;
    }
}

void InboxWirePlanner::refuse_queued(CarouselSession &session, int kind, int64_t now_ms)
{
    if (kind == kInboxWireInbox) {
        inbox_inflight_ = 0;
        inbox_quiet_until_ms_ = now_ms + kQueueRefuseBackoffMs;
        if (took_inbox_arm_) {
            session.arm_inbox_fetch();
        }
        took_inbox_arm_ = 0;
        return;
    }
    if (kind == kInboxWireLastViewed) {
        last_viewed_inflight_ = 0;
        last_viewed_quiet_until_ms_ = now_ms + kQueueRefuseBackoffMs;
        return;
    }
    if (kind == kInboxWireAudio) {
        audio_inflight_ = 0;
        audio_quiet_until_ms_ = now_ms + kQueueRefuseBackoffMs;
        return;
    }
    if (kind == kInboxWireSketch) {
        sketch_inflight_ = 0;
        sketch_quiet_until_ms_ = now_ms + kQueueRefuseBackoffMs;
    }
}

void InboxWirePlanner::apply_inbox(CarouselSession &session, uint32_t generation, int perform_ok, int http_status,
                                   const char *body, int body_len, int64_t now_ms)
{
    session.retain_inbox_arm_on_snapshot(session.inbox_fetch_armed());
    inbox_inflight_ = 0;
    took_inbox_arm_ = 0;
    (void)session.deliver_inbox_worker_result(generation, perform_ok, http_status, body, body_len, now_ms);
    session.retain_inbox_arm_on_snapshot(0);
}

void InboxWirePlanner::apply_inbox_parsed(CarouselSession &session, uint32_t generation,
                                          const InboxParseResult *parsed, int64_t now_ms)
{
    session.retain_inbox_arm_on_snapshot(session.inbox_fetch_armed());
    inbox_inflight_ = 0;
    took_inbox_arm_ = 0;
    (void)session.apply_inbox_parsed(generation, parsed, now_ms);
    session.retain_inbox_arm_on_snapshot(0);
}

void InboxWirePlanner::apply_last_viewed(CarouselSession &session, uint32_t generation, const char *body,
                                         int body_len, int perform_ok, int http_status, int64_t now_ms)
{
    int matches = 1;
    int identity_conflict;
    (void)now_ms;
    if (body != 0 && body_len > 0 && last_viewed_uuid_[0] != 0 && strstr(body, last_viewed_uuid_) == 0) {
        matches = 0;
    }
    if (!matches) {
        /* A late body for an older operation must not move selection or replace the current write. */
        return;
    }
    last_viewed_inflight_ = 0;
    if (generation == 0 || generation != session.session_generation()) {
        return;
    }
    if (perform_ok == 0 || http_status == 0) {
        return;
    }
    identity_conflict = http_status == 409 || (body != 0 && strstr(body, "identity_conflict") != 0);
    if (http_status == 200 && !identity_conflict) {
        have_last_viewed_body_ = 0;
        last_viewed_body_len_ = 0;
        copy_text(last_viewed_acknowledged_, kMaxMessageId, last_viewed_message_);
        return;
    }
    if (identity_conflict || http_status == 404 || http_status == 410) {
        have_last_viewed_body_ = 0;
        last_viewed_body_len_ = 0;
        copy_text(last_viewed_suppress_, kMaxMessageId, last_viewed_message_);
        return;
    }
}

void InboxWirePlanner::apply_audio(CarouselSession &session, uint32_t generation, uint32_t operation_id,
                                   const char *message_id, int perform_ok, int http_status, const uint8_t *bytes,
                                   int len, int full_asset, int hash_matches, int terminal, int64_t now_ms)
{
    if (terminal) {
        audio_inflight_ = 0;
    }
    (void)session.apply_audio_fetch_result(generation, operation_id, message_id, perform_ok, http_status, bytes,
                                           len, full_asset, hash_matches, now_ms);
}

void InboxWirePlanner::apply_sketch(CarouselSession &session, uint32_t generation, uint32_t operation_id,
                                    const char *message_id, int perform_ok, int http_status, const uint8_t *bytes,
                                    int len, int full_asset, int hash_matches, int terminal, int64_t now_ms)
{
    if (terminal) {
        sketch_inflight_ = 0;
    }
    (void)session.apply_sketch_fetch_result(generation, operation_id, message_id, perform_ok, http_status, bytes,
                                            len, full_asset, hash_matches, now_ms);
}

} /* namespace fl_product_inbox */
