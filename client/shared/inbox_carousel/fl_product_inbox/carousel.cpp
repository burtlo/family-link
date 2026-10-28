#include "fl_product_inbox_carousel.hpp"

#include "fl_product_inbox_flsk2.hpp"
#include "fl_product_inbox_p1_wire.hpp"

#include <stdlib.h>
#include <string.h>

namespace fl_product_inbox
{
namespace
{

bool text_eq(const char *a, const char *b)
{
    if (a == 0 || b == 0) {
        return false;
    }
    return strcmp(a, b) == 0;
}

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

int find_message_index(const InboxMessage *messages, int count, const char *message_id)
{
    int i;
    if (message_id == 0 || message_id[0] == 0) {
        return -1;
    }
    for (i = 0; i < count; ++i) {
        if (text_eq(messages[i].message_id, message_id)) {
            return i;
        }
    }
    return -1;
}

void copy_message(InboxMessage *dst, const InboxMessage *src)
{
    memcpy(dst, src, sizeof(*dst));
}

} /* namespace */

CarouselSession::CarouselSession()
{
    int i;
    for (i = 0; i < kMaxMessages; ++i) {
        runtime_[i].sketch_bytes = 0;
    }
    reset_fields(0);
}

CarouselSession::~CarouselSession()
{
    release_sketch_buffers();
}

void CarouselSession::reset_fields(int64_t now_ms)
{
    int i;
    wifi_up_ = 0;
    ui_visible_ = 1;
    has_valid_inbox_ = 0;
    inbox_revision_ = 0;
    session_generation_ = 0;
    message_count_ = 0;
    memset(messages_, 0, sizeof(messages_));
    for (i = 0; i < kMaxMessages; ++i) {
        runtime_[i].play_head_ms = 0;
        runtime_[i].completed = 0;
        runtime_[i].paused = 0;
        runtime_[i].audio_buffered = 0;
        runtime_[i].sketch_bytes = 0;
        runtime_[i].sketch_len = 0;
    }
    selected_index_ = -1;
    selected_message_id_[0] = 0;
    selection_settled_ms_ = 0;
    pending_last_viewed_[0] = 0;
    last_viewed_due_ms_ = 0;
    next_ordinary_fetch_ms_ = now_ms;
    next_recovery_fetch_ms_ = now_ms + kConnectingTimeoutMs;
    inbox_fetch_armed_ = 0;
    snapshot_keeps_arm_ = 0;
    recovery_pulse_ = 0;
    connecting_ = 0;
    connecting_wait_index_ = -1;
    connecting_retry_due_ms_ = 0;
    last_update_level_ = 0;
    transport_ = kTransportHidden;
    playback_ = kPlaybackIdle;
    playback_operation_id_ = 0;
    next_playback_operation_ = 1;
    playback_message_id_[0] = 0;
    cant_play_until_ms_ = 0;
    sketch_visible_ = 0;
    portrait_hidden_ = 0;
    sketch_rendered_ = 0;
    memset(sketch_pixels_, 0xff, sizeof(sketch_pixels_));
    settings_pause_active_ = 0;
}

void CarouselSession::release_sketch_buffers()
{
    int i;
    for (i = 0; i < kMaxMessages; ++i) {
        if (runtime_[i].sketch_bytes != 0) {
            free(runtime_[i].sketch_bytes);
            runtime_[i].sketch_bytes = 0;
        }
        runtime_[i].sketch_len = 0;
    }
}

void CarouselSession::clear_runtime_slot(MessageRuntime *slot)
{
    if (slot == 0) {
        return;
    }
    if (slot->sketch_bytes != 0) {
        free(slot->sketch_bytes);
        slot->sketch_bytes = 0;
    }
    slot->play_head_ms = 0;
    slot->completed = 0;
    slot->paused = 0;
    slot->audio_buffered = 0;
    slot->sketch_len = 0;
}

void CarouselSession::boot(int64_t now_ms)
{
    release_sketch_buffers();
    reset_fields(now_ms);
}

void CarouselSession::cancel_playback_operation()
{
    playback_operation_id_ = 0;
    playback_message_id_[0] = 0;
}

void CarouselSession::assign_playback_operation(const char *message_id)
{
    uint32_t id = next_playback_operation_;
    if (id == 0u) {
        id = 1u;
    }
    next_playback_operation_ = id + 1u;
    if (next_playback_operation_ == 0u) {
        next_playback_operation_ = 1u;
    }
    playback_operation_id_ = id;
    copy_text(playback_message_id_, kMaxMessageId, message_id);
}

void CarouselSession::pause_active_playback()
{
    if (playback_ != kPlaybackPlaying && playback_ != kPlaybackBuffering &&
        playback_ != kPlaybackCantPlay) {
        return;
    }
    if (selected_index_ >= 0 && selected_index_ < message_count_) {
        if (!runtime_[selected_index_].completed) {
            runtime_[selected_index_].paused = 1;
        }
    }
    cant_play_until_ms_ = 0;
    playback_ = kPlaybackPaused;
    if (selected_index_ >= 0) {
        transport_ = kTransportPlay;
    } else {
        transport_ = kTransportHidden;
    }
    cancel_playback_operation();
}

void CarouselSession::begin_connecting(int64_t now_ms)
{
    pause_active_playback();
    if (!wifi_up_) {
        return;
    }
    if (!connecting_) {
        connecting_ = 1;
        connecting_wait_index_ = -1;
        connecting_retry_due_ms_ = now_ms + kConnectingTimeoutMs;
        if (!inbox_fetch_armed_) {
            next_recovery_fetch_ms_ = now_ms + kConnectingTimeoutMs;
        }
    }
    /* A refresh or Wi-Fi return already made this recovery due. Do not push it to +3000. */
    if (inbox_fetch_armed_ && next_recovery_fetch_ms_ > now_ms) {
        next_recovery_fetch_ms_ = now_ms;
    }
}

void CarouselSession::set_wifi(bool up, int64_t now_ms)
{
    int next = up ? 1 : 0;
    int became_up;
    if (!next) {
        wifi_up_ = 0;
        pause_active_playback();
        return;
    }
    became_up = wifi_up_ ? 0 : 1;
    wifi_up_ = 1;
    if (!became_up) {
        return;
    }
    /* Connectivity return adds one inbox GET, including while connecting is showing. */
    inbox_fetch_armed_ = 1;
    if (has_valid_inbox_) {
        connecting_ = 0;
        connecting_wait_index_ = -1;
        return;
    }
    connecting_ = 0;
    begin_connecting(now_ms);
}

void CarouselSession::set_session_generation(uint32_t generation)
{
    session_generation_ = generation;
}

CarouselSession::MessageRuntime *CarouselSession::selected_runtime()
{
    if (selected_index_ < 0 || selected_index_ >= message_count_) {
        return 0;
    }
    return &runtime_[selected_index_];
}

const CarouselSession::MessageRuntime *CarouselSession::selected_runtime() const
{
    if (selected_index_ < 0 || selected_index_ >= message_count_) {
        return 0;
    }
    return &runtime_[selected_index_];
}

void CarouselSession::repaint_selected_sketch()
{
    const MessageRuntime *rt = selected_runtime();
    int aligned;
    uint32_t position_ms;
    sketch_rendered_ = 0;
    sketch_visible_ = 0;
    portrait_hidden_ = 0;
    memset(sketch_pixels_, 0xff, sizeof(sketch_pixels_));
    if (rt == 0 || rt->sketch_bytes == 0 || rt->sketch_len < 8) {
        return;
    }
    aligned = rt->sketch_len - (rt->sketch_len % 8);
    if (aligned < 8) {
        return;
    }
    if (rt->completed) {
        position_ms = 0xffffffffu;
    } else if (rt->play_head_ms > 0) {
        position_ms = (uint32_t)rt->play_head_ms;
    } else {
        position_ms = 0;
    }
    flsk2_paint_prefix(sketch_pixels_, kSketchPixels, rt->sketch_bytes, (size_t)aligned, position_ms,
                       &sketch_rendered_);
    if (sketch_rendered_ > 0) {
        sketch_visible_ = 1;
        portrait_hidden_ = 1;
    }
}

void CarouselSession::sync_phase_from_selected()
{
    const MessageRuntime *rt;
    if (selected_index_ < 0) {
        playback_ = kPlaybackIdle;
        transport_ = kTransportHidden;
        repaint_selected_sketch();
        return;
    }
    rt = selected_runtime();
    if (rt != 0 && rt->paused && !rt->completed) {
        playback_ = kPlaybackPaused;
    } else {
        playback_ = kPlaybackIdle;
    }
    transport_ = kTransportPlay;
    repaint_selected_sketch();
}

void CarouselSession::apply_higher_revision(const InboxParseResult &parsed, int64_t now_ms)
{
    char old_ids[kMaxMessages][kMaxMessageId];
    int claimed[kMaxMessages];
    MessageRuntime fresh[kMaxMessages];
    char prior_id[kMaxMessageId];
    int old_count = message_count_;
    int old_sel = selected_index_;
    int i;
    int j;
    int keep;
    int fallback;

    prior_id[0] = 0;
    if (old_sel >= 0 && old_sel < old_count) {
        copy_text(prior_id, kMaxMessageId, messages_[old_sel].message_id);
    }
    for (i = 0; i < kMaxMessages; ++i) {
        old_ids[i][0] = 0;
        claimed[i] = 0;
        fresh[i].play_head_ms = 0;
        fresh[i].completed = 0;
        fresh[i].paused = 0;
        fresh[i].audio_buffered = 0;
        fresh[i].sketch_bytes = 0;
        fresh[i].sketch_len = 0;
    }
    for (i = 0; i < old_count; ++i) {
        copy_text(old_ids[i], kMaxMessageId, messages_[i].message_id);
    }
    for (i = 0; i < parsed.message_count && i < kMaxMessages; ++i) {
        int found = -1;
        for (j = 0; j < old_count; ++j) {
            if (!claimed[j] && text_eq(old_ids[j], parsed.messages[i].message_id)) {
                found = j;
                break;
            }
        }
        if (found >= 0) {
            fresh[i] = runtime_[found];
            runtime_[found].sketch_bytes = 0;
            runtime_[found].sketch_len = 0;
            claimed[found] = 1;
        }
    }
    for (j = 0; j < old_count; ++j) {
        if (!claimed[j]) {
            clear_runtime_slot(&runtime_[j]);
        }
    }
    for (i = 0; i < parsed.message_count && i < kMaxMessages; ++i) {
        copy_message(&messages_[i], &parsed.messages[i]);
        runtime_[i] = fresh[i];
    }
    for (i = parsed.message_count; i < kMaxMessages; ++i) {
        runtime_[i].play_head_ms = 0;
        runtime_[i].completed = 0;
        runtime_[i].paused = 0;
        runtime_[i].audio_buffered = 0;
        runtime_[i].sketch_bytes = 0;
        runtime_[i].sketch_len = 0;
        messages_[i].message_id[0] = 0;
    }
    message_count_ = parsed.message_count;
    if (message_count_ > kMaxMessages) {
        message_count_ = kMaxMessages;
    }
    inbox_revision_ = parsed.inbox_revision;

    if (prior_id[0] == 0) {
        cancel_playback_operation();
        playback_ = kPlaybackIdle;
        selected_index_ = -1;
        selected_message_id_[0] = 0;
        if (message_count_ > 0) {
            select_index(message_count_ - 1, now_ms);
        } else {
            transport_ = kTransportHidden;
            repaint_selected_sketch();
        }
        return;
    }

    keep = find_message_index(messages_, message_count_, prior_id);
    if (keep >= 0) {
        selected_index_ = keep;
        copy_text(selected_message_id_, kMaxMessageId, prior_id);
        return;
    }

    cancel_playback_operation();
    cant_play_until_ms_ = 0;
    playback_ = kPlaybackIdle;
    selected_index_ = -1;
    selected_message_id_[0] = 0;
    fallback = -1;
    if (old_sel <= 0) {
        for (j = 1; j < old_count; ++j) {
            int idx = find_message_index(messages_, message_count_, old_ids[j]);
            if (idx >= 0) {
                fallback = idx;
                break;
            }
        }
    } else {
        for (j = old_sel - 1; j >= 0; --j) {
            int idx = find_message_index(messages_, message_count_, old_ids[j]);
            if (idx >= 0) {
                fallback = idx;
                break;
            }
        }
        if (fallback < 0) {
            for (j = old_sel + 1; j < old_count; ++j) {
                int idx = find_message_index(messages_, message_count_, old_ids[j]);
                if (idx >= 0) {
                    fallback = idx;
                    break;
                }
            }
        }
    }
    if (fallback >= 0) {
        select_index(fallback, now_ms);
    } else if (message_count_ > 0) {
        select_index(message_count_ - 1, now_ms);
    } else {
        transport_ = kTransportHidden;
        playback_ = kPlaybackIdle;
        repaint_selected_sketch();
    }
}

int CarouselSession::apply_parsed_snapshot(const InboxParseResult &parsed, int64_t now_ms)
{
    int i;
    if (parsed.overflow || parsed.message_count > kMaxMessages) {
        begin_connecting(now_ms);
        return 0;
    }
    if (!has_valid_inbox_) {
        release_sketch_buffers();
        for (i = 0; i < kMaxMessages; ++i) {
            clear_runtime_slot(&runtime_[i]);
        }
        message_count_ = parsed.message_count;
        for (i = 0; i < message_count_; ++i) {
            copy_message(&messages_[i], &parsed.messages[i]);
            runtime_[i].play_head_ms = 0;
            runtime_[i].completed = 0;
            runtime_[i].paused = 0;
            runtime_[i].audio_buffered = 0;
            runtime_[i].sketch_bytes = 0;
            runtime_[i].sketch_len = 0;
        }
        inbox_revision_ = parsed.inbox_revision;
        has_valid_inbox_ = 1;
        connecting_ = 0;
        connecting_wait_index_ = -1;
        last_update_level_ = 1;
        next_ordinary_fetch_ms_ = now_ms + kInboxFetchPeriodMs;
        cancel_playback_operation();
        playback_ = kPlaybackIdle;
        selected_index_ = -1;
        selected_message_id_[0] = 0;
        if (message_count_ == 0) {
            transport_ = kTransportHidden;
            repaint_selected_sketch();
            release_snapshot_arm();
            return 1;
        }
        i = find_message_index(messages_, message_count_, parsed.last_viewed_message_id);
        if (i < 0) {
            i = message_count_ - 1;
        }
        select_index(i, now_ms);
        release_snapshot_arm();
        return 1;
    }
    if (parsed.inbox_revision < inbox_revision_) {
        return 0;
    }
    if (parsed.inbox_revision == inbox_revision_) {
        for (i = 0; i < parsed.message_count; ++i) {
            int idx = find_message_index(messages_, message_count_, parsed.messages[i].message_id);
            if (idx >= 0) {
                copy_text(messages_[idx].sender_name, kMaxName, parsed.messages[i].sender_name);
                copy_text(messages_[idx].sender_avatar_id, kMaxAvatarId, parsed.messages[i].sender_avatar_id);
                copy_text(messages_[idx].sender_card_background_id, kMaxCardBgId,
                          parsed.messages[i].sender_card_background_id);
            }
        }
        last_update_level_ = 1;
        connecting_ = 0;
        connecting_wait_index_ = -1;
        release_snapshot_arm();
        return 1;
    }
    apply_higher_revision(parsed, now_ms);
    last_update_level_ = 1;
    connecting_ = 0;
    connecting_wait_index_ = -1;
    next_ordinary_fetch_ms_ = now_ms + kInboxFetchPeriodMs;
    release_snapshot_arm();
    return 1;
}

int CarouselSession::deliver_inbox_worker_result(uint32_t worker_generation, int perform_ok,
                                                 int http_status, const char *body, int body_len,
                                                 int64_t now_ms)
{
    InboxParseResult *parsed;
    int applied;
    /* InboxParseResult holds 64 messages and does not fit on the LVGL task stack. */
    if (worker_generation == 0 || worker_generation != session_generation_) {
        return 0;
    }
    if (!classify_inbox_http(perform_ok, http_status, body, body_len)) {
        begin_connecting(now_ms);
        return 0;
    }
    parsed = (InboxParseResult *)malloc(sizeof(*parsed));
    if (parsed == 0) {
        begin_connecting(now_ms);
        return 0;
    }
    if (!parse_inbox_snapshot(body, body_len, body_len, parsed) || parsed->overflow) {
        begin_connecting(now_ms);
        free(parsed);
        return 0;
    }
    applied = apply_parsed_snapshot(*parsed, now_ms);
    free(parsed);
    return applied;
}

int CarouselSession::apply_inbox_parsed(uint32_t worker_generation, const InboxParseResult *parsed,
                                        int64_t now_ms)
{
    if (worker_generation == 0 || worker_generation != session_generation_ || parsed == 0) {
        return 0;
    }
    if (!parsed->parse_ok || parsed->overflow || parsed->oversize) {
        begin_connecting(now_ms);
        return 0;
    }
    return apply_parsed_snapshot(*parsed, now_ms);
}

void CarouselSession::select_index(int index, int64_t now_ms)
{
    if (index < 0 || index >= message_count_) {
        return;
    }
    if (index == selected_index_ && text_eq(selected_message_id_, messages_[index].message_id)) {
        return;
    }
    if (selected_index_ >= 0 && selected_index_ < message_count_) {
        if (playback_ == kPlaybackPlaying || playback_ == kPlaybackBuffering) {
            if (!runtime_[selected_index_].completed) {
                runtime_[selected_index_].paused = 1;
            }
        }
    }
    cancel_playback_operation();
    cant_play_until_ms_ = 0;
    selected_index_ = index;
    copy_text(selected_message_id_, kMaxMessageId, messages_[index].message_id);
    selection_settled_ms_ = now_ms;
    pending_last_viewed_[0] = 0;
    schedule_last_viewed(now_ms);
    sync_phase_from_selected();
}

void CarouselSession::schedule_last_viewed(int64_t now_ms)
{
    last_viewed_due_ms_ = now_ms + kLastViewedCoalesceMs;
}

void CarouselSession::maybe_emit_last_viewed(int64_t now_ms)
{
    if (selected_message_id_[0] == 0) {
        return;
    }
    if (now_ms < last_viewed_due_ms_) {
        return;
    }
    if (!text_eq(pending_last_viewed_, selected_message_id_)) {
        copy_text(pending_last_viewed_, kMaxMessageId, selected_message_id_);
    }
}

void CarouselSession::advance_to(int64_t now_ms)
{
    static const int kWaitMs[] = {1000, 2000, 4000, 30000};
    int idx;
    int hold_armed_due;
    maybe_emit_last_viewed(now_ms);
    hold_armed_due = inbox_fetch_armed_ && next_recovery_fetch_ms_ <= now_ms;
    if (ui_visible_ && connecting_ && wifi_up_ && !hold_armed_due && connecting_retry_due_ms_ > 0 &&
        now_ms >= connecting_retry_due_ms_) {
        recovery_pulse_ = 1;
        if (connecting_wait_index_ < 0) {
            connecting_wait_index_ = 0;
        }
        idx = connecting_wait_index_;
        if (idx > 3) {
            idx = 3;
        }
        connecting_retry_due_ms_ = now_ms + kWaitMs[idx];
        next_recovery_fetch_ms_ = connecting_retry_due_ms_;
        if (connecting_wait_index_ < 3) {
            connecting_wait_index_ += 1;
        }
    }
    if (cant_play_until_ms_ > 0 && now_ms >= cant_play_until_ms_) {
        cant_play_until_ms_ = 0;
        if (playback_ == kPlaybackCantPlay) {
            playback_ = kPlaybackPaused;
            if (selected_index_ >= 0 && selected_index_ < message_count_ &&
                !runtime_[selected_index_].completed) {
                runtime_[selected_index_].paused = 1;
            }
            transport_ = kTransportPlay;
        }
    }
}

void CarouselSession::update_transport_for_playback()
{
    if (selected_index_ < 0) {
        transport_ = kTransportHidden;
        return;
    }
    if (playback_ == kPlaybackPlaying) {
        transport_ = kTransportPause;
    } else {
        transport_ = kTransportPlay;
    }
}

int CarouselSession::toggle_transport(int64_t now_ms)
{
    MessageRuntime *rt;
    const InboxMessage *msg;
    (void)now_ms;
    if (play_disabled()) {
        return 0;
    }
    if (playback_ == kPlaybackPlaying) {
        if (selected_index_ >= 0) {
            runtime_[selected_index_].paused = 1;
        }
        playback_ = kPlaybackPaused;
        transport_ = kTransportPlay;
        cancel_playback_operation();
        return 1;
    }
    if (selected_index_ < 0) {
        return 0;
    }
    rt = &runtime_[selected_index_];
    msg = &messages_[selected_index_];
    if (rt->completed) {
        rt->completed = 0;
        rt->paused = 0;
        rt->play_head_ms = 0;
        repaint_selected_sketch();
    }
    rt->paused = 0;
    assign_playback_operation(msg->message_id);
    if (rt->audio_buffered < (int)kAudioPrefixBytes && msg->audio.byte_count > kAudioPrefixBytes) {
        playback_ = kPlaybackBuffering;
    } else if (rt->audio_buffered > 0) {
        playback_ = kPlaybackPlaying;
    } else {
        playback_ = kPlaybackBuffering;
    }
    update_transport_for_playback();
    return 1;
}

int CarouselSession::deliver_audio_chunk(uint32_t operation_id, const char *message_id,
                                         const uint8_t *bytes, int len, int full_asset, int hash_matches,
                                         int64_t now_ms)
{
    MessageRuntime *rt;
    const InboxMessage *msg;
    if (operation_id == 0 || operation_id != playback_operation_id_) {
        return 0;
    }
    if (message_id == 0 || !text_eq(message_id, playback_message_id_)) {
        return 0;
    }
    if (!wifi_up_ || connecting_) {
        return 0;
    }
    if (bytes == 0 || len <= 0 || selected_index_ < 0) {
        return 0;
    }
    if (!text_eq(messages_[selected_index_].message_id, message_id)) {
        return 0;
    }
    rt = &runtime_[selected_index_];
    msg = &messages_[selected_index_];
    rt->audio_buffered += len;
    if (full_asset && !hash_matches) {
        playback_ = kPlaybackCantPlay;
        cant_play_until_ms_ = now_ms + kCantPlayMs;
        transport_ = kTransportPlay;
        cancel_playback_operation();
        return 0;
    }
    if (playback_ == kPlaybackBuffering && prefix_play_ready()) {
        playback_ = kPlaybackPlaying;
        update_transport_for_playback();
    }
    if (playback_ == kPlaybackPlaying && msg->audio.byte_count > 0 && msg->duration_ms > 0) {
        int64_t head = ((int64_t)rt->audio_buffered * (int64_t)msg->duration_ms) /
                       (int64_t)msg->audio.byte_count;
        if (head > (int64_t)msg->duration_ms) {
            head = (int64_t)msg->duration_ms;
        }
        if (rt->audio_buffered >= (int)msg->audio.byte_count || head >= (int64_t)msg->duration_ms) {
            rt->play_head_ms = (int)msg->duration_ms;
            rt->completed = 1;
            rt->paused = 0;
            playback_ = kPlaybackIdle;
            transport_ = kTransportPlay;
            cancel_playback_operation();
            repaint_selected_sketch();
        } else {
            rt->play_head_ms = (int)head;
            repaint_selected_sketch();
        }
    }
    return 1;
}

int CarouselSession::deliver_sketch_chunk(uint32_t operation_id, const char *message_id,
                                          const uint8_t *bytes, int len, int64_t now_ms)
{
    MessageRuntime *rt;
    const InboxMessage *msg;
    int new_len;
    (void)now_ms;
    if (operation_id == 0 || operation_id != playback_operation_id_) {
        return 0;
    }
    if (message_id == 0 || !text_eq(message_id, playback_message_id_)) {
        return 0;
    }
    if (!wifi_up_ || connecting_ || bytes == 0 || len <= 0 || selected_index_ < 0) {
        return 0;
    }
    if (!text_eq(messages_[selected_index_].message_id, message_id)) {
        return 0;
    }
    msg = &messages_[selected_index_];
    rt = &runtime_[selected_index_];
    if (msg == 0 || !msg->sketch.present) {
        return 0;
    }
    new_len = rt->sketch_len + len;
    if (new_len > kSketchBufferCap || new_len > (int)msg->sketch.byte_count) {
        return 0;
    }
    if (rt->sketch_bytes == 0) {
        rt->sketch_bytes = (uint8_t *)malloc((size_t)kSketchBufferCap);
        if (rt->sketch_bytes == 0) {
            return 0;
        }
    }
    memcpy(rt->sketch_bytes + rt->sketch_len, bytes, (size_t)len);
    if (new_len % 8 == 0 && !flsk2_validate(rt->sketch_bytes, (size_t)new_len, 0)) {
        return 0;
    }
    rt->sketch_len = new_len;
    repaint_selected_sketch();
    return 1;
}

void CarouselSession::on_mute(int64_t now_ms)
{
    int i;
    uint32_t next;
    (void)now_ms;
    release_sketch_buffers();
    for (i = 0; i < kMaxMessages; ++i) {
        runtime_[i].play_head_ms = 0;
        runtime_[i].completed = 0;
        runtime_[i].paused = 0;
        runtime_[i].audio_buffered = 0;
        runtime_[i].sketch_len = 0;
        runtime_[i].sketch_bytes = 0;
    }
    cancel_playback_operation();
    playback_ = kPlaybackIdle;
    transport_ = kTransportHidden;
    connecting_ = 0;
    connecting_wait_index_ = -1;
    settings_pause_active_ = 0;
    cant_play_until_ms_ = 0;
    sketch_visible_ = 0;
    portrait_hidden_ = 0;
    sketch_rendered_ = 0;
    memset(sketch_pixels_, 0xff, sizeof(sketch_pixels_));
    next = session_generation_ + 1u;
    if (next == 0u) {
        next = 1u;
    }
    session_generation_ = next;
}

void CarouselSession::pause_for_settings(int64_t now_ms)
{
    (void)now_ms;
    pause_active_playback();
    settings_pause_active_ = 1;
}

void CarouselSession::clear_settings_pause(int64_t now_ms)
{
    (void)now_ms;
    settings_pause_active_ = 0;
}

void CarouselSession::pause_for_record(int64_t now_ms)
{
    (void)now_ms;
    pause_active_playback();
}

void CarouselSession::note_refresh_success(int64_t now_ms)
{
    inbox_fetch_armed_ = 1;
    if (connecting_ && next_recovery_fetch_ms_ > now_ms) {
        next_recovery_fetch_ms_ = now_ms;
    }
}

void CarouselSession::note_refresh_failure(int64_t now_ms)
{
    begin_connecting(now_ms);
}

int CarouselSession::message_count() const
{
    return message_count_;
}
int CarouselSession::selected_index() const
{
    return selected_index_;
}
const char *CarouselSession::selected_message_id() const
{
    return selected_message_id_;
}

const InboxMessage *CarouselSession::message_at(int index) const
{
    if (index < 0 || index >= message_count_) {
        return 0;
    }
    return &messages_[index];
}

uint32_t CarouselSession::inbox_revision() const
{
    return inbox_revision_;
}
int CarouselSession::has_valid_inbox() const
{
    return has_valid_inbox_;
}

ConnectivitySlot CarouselSession::connectivity_slot() const
{
    if (!wifi_up_) {
        return kSlotNoWifi;
    }
    if (connecting_) {
        return kSlotConnecting;
    }
    return kSlotNone;
}

TransportState CarouselSession::transport_state() const
{
    return transport_;
}
PlaybackPhase CarouselSession::playback_phase() const
{
    return playback_;
}
uint32_t CarouselSession::playback_operation_id() const
{
    return playback_operation_id_;
}

int CarouselSession::play_disabled() const
{
    if (!wifi_up_ || connecting_ || selected_index_ < 0) {
        return 1;
    }
    return 0;
}

int CarouselSession::sketch_visible() const
{
    return sketch_visible_;
}
int CarouselSession::portrait_hidden() const
{
    return portrait_hidden_;
}
int CarouselSession::unread_badge_shown() const
{
    return 0;
}
int CarouselSession::last_update_level() const
{
    return last_update_level_;
}

int CarouselSession::inbox_fetch_due(int64_t now_ms) const
{
    if (!ui_visible_ || !wifi_up_ || connecting_) {
        return 0;
    }
    return now_ms >= next_ordinary_fetch_ms_ ? 1 : 0;
}

int CarouselSession::recovery_fetch_due(int64_t now_ms) const
{
    if (!ui_visible_ || !wifi_up_ || !connecting_) {
        return 0;
    }
    return now_ms >= next_recovery_fetch_ms_ ? 1 : 0;
}

int CarouselSession::inbox_fetch_armed() const
{
    return inbox_fetch_armed_;
}

int CarouselSession::ui_visible() const
{
    return ui_visible_;
}

void CarouselSession::set_ui_visible(int visible)
{
    ui_visible_ = visible ? 1 : 0;
}

int CarouselSession::media_fetch_allowed() const
{
    if (!wifi_up_ || connecting_) {
        return 0;
    }
    return 1;
}

int CarouselSession::recovery_pulse_pending() const
{
    return recovery_pulse_;
}

void CarouselSession::clear_recovery_pulse()
{
    recovery_pulse_ = 0;
}

void CarouselSession::arm_inbox_fetch()
{
    inbox_fetch_armed_ = 1;
}

void CarouselSession::consume_inbox_arm()
{
    inbox_fetch_armed_ = 0;
}

void CarouselSession::retain_inbox_arm_on_snapshot(int retain)
{
    snapshot_keeps_arm_ = retain ? 1 : 0;
}

void CarouselSession::release_snapshot_arm()
{
    if (!snapshot_keeps_arm_) {
        inbox_fetch_armed_ = 0;
    }
}

void CarouselSession::acknowledge_inbox_fetch(int64_t now_ms)
{
    /* The arm is consumed when its own GET is queued. Accepting some other GET must not drop it. */
    if (connecting_) {
        if (next_recovery_fetch_ms_ <= now_ms && connecting_retry_due_ms_ > now_ms) {
            next_recovery_fetch_ms_ = connecting_retry_due_ms_;
        }
        return;
    }
    if (now_ms >= next_ordinary_fetch_ms_) {
        next_ordinary_fetch_ms_ = now_ms + kInboxFetchPeriodMs;
    }
}

uint32_t CarouselSession::session_generation() const
{
    return session_generation_;
}

int CarouselSession::sketch_bytes_stored() const
{
    const MessageRuntime *rt = selected_runtime();
    if (rt == 0) {
        return 0;
    }
    return rt->sketch_len;
}

int CarouselSession::apply_audio_fetch_result(uint32_t generation, uint32_t operation_id,
                                              const char *message_id, int perform_ok, int http_status,
                                              const uint8_t *bytes, int len, int full_asset,
                                              int hash_matches, int64_t now_ms)
{
    if (generation == 0 || generation != session_generation_) {
        return 0;
    }
    if (http_status == 401 || http_status == 403) {
        begin_connecting(now_ms);
        return 0;
    }
    if (!wifi_up_ || connecting_) {
        return 0;
    }
    if (operation_id == 0 || operation_id != playback_operation_id_) {
        return 0;
    }
    if (!perform_ok || http_status < 200 || http_status >= 300) {
        if (playback_ == kPlaybackPlaying || playback_ == kPlaybackBuffering) {
            playback_ = kPlaybackCantPlay;
            cant_play_until_ms_ = now_ms + kCantPlayMs;
            transport_ = kTransportPlay;
            if (selected_index_ >= 0 && selected_index_ < message_count_ &&
                !runtime_[selected_index_].completed) {
                runtime_[selected_index_].paused = 1;
            }
            cancel_playback_operation();
        }
        return 0;
    }
    if (len <= 0) {
        if (full_asset && !hash_matches) {
            playback_ = kPlaybackCantPlay;
            cant_play_until_ms_ = now_ms + kCantPlayMs;
            transport_ = kTransportPlay;
            cancel_playback_operation();
        }
        return 0;
    }
    return deliver_audio_chunk(operation_id, message_id, bytes, len, full_asset, hash_matches, now_ms);
}

int CarouselSession::apply_sketch_fetch_result(uint32_t generation, uint32_t operation_id,
                                               const char *message_id, int perform_ok, int http_status,
                                               const uint8_t *bytes, int len, int full_asset,
                                               int hash_matches, int64_t now_ms)
{
    if (generation == 0 || generation != session_generation_) {
        return 0;
    }
    if (http_status == 401 || http_status == 403) {
        begin_connecting(now_ms);
        return 0;
    }
    /* A sketch failure does not abort audio. */
    if (!wifi_up_ || connecting_ || operation_id == 0 || operation_id != playback_operation_id_) {
        return 0;
    }
    if (!perform_ok || http_status < 200 || http_status >= 300 || (full_asset && !hash_matches)) {
        return 0;
    }
    if (bytes == 0 || len <= 0) {
        return 0;
    }
    return deliver_sketch_chunk(operation_id, message_id, bytes, len, now_ms);
}

const char *CarouselSession::pending_last_viewed_message_id() const
{
    return pending_last_viewed_;
}

int CarouselSession::last_viewed_write_due(int64_t now_ms) const
{
    return pending_last_viewed_[0] != 0 && now_ms >= last_viewed_due_ms_ ? 1 : 0;
}

int CarouselSession::audio_bytes_buffered() const
{
    const MessageRuntime *rt = selected_runtime();
    if (rt == 0) {
        return 0;
    }
    return rt->audio_buffered;
}

int CarouselSession::prefix_play_ready() const
{
    const MessageRuntime *rt;
    const InboxMessage *msg;
    if (selected_index_ < 0) {
        return 0;
    }
    rt = selected_runtime();
    msg = message_at(selected_index_);
    if (rt == 0 || msg == 0) {
        return 0;
    }
    if (msg->audio.byte_count <= kAudioPrefixBytes) {
        return rt->audio_buffered >= (int)msg->audio.byte_count;
    }
    return rt->audio_buffered >= (int)kAudioPrefixBytes;
}

uint16_t CarouselSession::sketch_points_rendered() const
{
    return sketch_rendered_;
}

int CarouselSession::play_head_ms() const
{
    const MessageRuntime *rt = selected_runtime();
    if (rt == 0) {
        return 0;
    }
    return rt->play_head_ms;
}

int CarouselSession::settings_pause_active() const
{
    return settings_pause_active_;
}

const uint16_t *CarouselSession::selected_sketch_pixels() const
{
    return sketch_pixels_;
}

int CarouselSession::message_sketch_len(int index) const
{
    if (index < 0 || index >= message_count_) {
        return 0;
    }
    return runtime_[index].sketch_len;
}

int CarouselSession::play_head_ms_at(int index) const
{
    if (index < 0 || index >= message_count_) {
        return 0;
    }
    return runtime_[index].play_head_ms;
}

int CarouselSession::message_completed_at(int index) const
{
    if (index < 0 || index >= message_count_) {
        return 0;
    }
    return runtime_[index].completed;
}

int CarouselSession::paint_message_sketch(int index, uint16_t *pixels, int pixel_count) const
{
    const MessageRuntime *rt;
    int aligned;
    uint32_t position_ms;
    uint16_t rendered;
    if (pixels == 0 || pixel_count < kSketchPixels || index < 0 || index >= message_count_) {
        return 0;
    }
    rt = &runtime_[index];
    rendered = 0;
    memset(pixels, 0xff, sizeof(uint16_t) * (size_t)kSketchPixels);
    if (rt->sketch_bytes == 0 || rt->sketch_len < 8) {
        return 0;
    }
    aligned = rt->sketch_len - (rt->sketch_len % 8);
    if (aligned < 8) {
        return 0;
    }
    if (rt->completed) {
        position_ms = 0xffffffffu;
    } else if (rt->play_head_ms > 0) {
        position_ms = (uint32_t)rt->play_head_ms;
    } else {
        position_ms = 0;
    }
    flsk2_paint_prefix(pixels, (size_t)kSketchPixels, rt->sketch_bytes, (size_t)aligned, position_ms,
                       &rendered);
    return rendered > 0 ? 1 : 0;
}

} /* namespace fl_product_inbox */
