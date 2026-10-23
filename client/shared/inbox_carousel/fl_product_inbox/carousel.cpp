#include "fl_product_inbox_carousel.hpp"

#include "fl_product_inbox_flsk2.hpp"
#include "fl_product_inbox_p1_wire.hpp"

#include <string.h>

namespace fl_product_inbox {
namespace {

bool text_eq(const char *a, const char *b)
{
    if (a == 0 || b == 0) {
        return false;
    }
    return strcmp(a, b) == 0;
}

int find_message_index(const InboxMessage *messages, int count, const char *message_id)
{
    int i;
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
    memset(this, 0, sizeof(*this));
    selected_index_ = -1;
    connecting_wait_index_ = -1;
}

void CarouselSession::boot(int64_t now_ms)
{
    memset(this, 0, sizeof(*this));
    selected_index_ = -1;
    connecting_wait_index_ = -1;
    wifi_up_ = 1;
    next_ordinary_fetch_ms_ = now_ms;
    next_recovery_fetch_ms_ = now_ms + kConnectingTimeoutMs;
}

void CarouselSession::set_wifi(bool up, int64_t now_ms)
{
    wifi_up_ = up ? 1 : 0;
    if (!wifi_up_) {
        pause_for_connectivity(now_ms);
    }
}

void CarouselSession::set_session_generation(uint32_t generation) { session_generation_ = generation; }

void CarouselSession::clear_media_buffers()
{
    audio_buffered_ = 0;
    audio_hash_ok_ = 0;
    sketch_buffered_ = 0;
    sketch_bytes_len_ = 0;
    sketch_rendered_ = 0;
    play_head_ms_ = 0;
    memset(sketch_bytes_, 0, sizeof(sketch_bytes_));
    memset(sketch_pixels_, 0xff, sizeof(sketch_pixels_));
    sketch_visible_ = 0;
    portrait_hidden_ = 0;
}

void CarouselSession::pause_for_connectivity(int64_t now_ms)
{
    if (playback_ == kPlaybackPlaying) {
        playback_ = kPlaybackPaused;
    }
    clear_media_buffers();
    transport_ = kTransportPlay;
    cant_play_until_ms_ = 0;
    connecting_ = wifi_up_ ? 1 : 0;
    if (connecting_ && connecting_wait_index_ < 0) {
        connecting_wait_index_ = 0;
        connecting_retry_due_ms_ = now_ms + kConnectingTimeoutMs;
        next_recovery_fetch_ms_ = now_ms + kConnectingTimeoutMs;
    }
}

int CarouselSession::apply_parsed_snapshot(const InboxParseResult &parsed, int64_t now_ms)
{
    int i;
    int old_count = message_count_;
    char prior_selected[kMaxMessageId];
    strncpy(prior_selected, selected_message_id_, sizeof(prior_selected) - 1);
    if (!has_valid_inbox_) {
        message_count_ = parsed.message_count;
        for (i = 0; i < message_count_; ++i) {
            copy_message(&messages_[i], &parsed.messages[i]);
        }
        inbox_revision_ = parsed.inbox_revision;
        has_valid_inbox_ = 1;
        if (parsed.last_viewed_message_id[0] != 0) {
            int idx = find_message_index(messages_, message_count_, parsed.last_viewed_message_id);
            if (idx >= 0) {
                select_index(idx, now_ms);
            } else if (message_count_ > 0) {
                select_index(message_count_ - 1, now_ms);
            }
        } else if (message_count_ > 0) {
            select_index(message_count_ - 1, now_ms);
        }
        last_update_level_ = 1;
        connecting_ = 0;
        connecting_wait_index_ = -1;
        next_ordinary_fetch_ms_ = now_ms + kInboxFetchPeriodMs;
        return 1;
    }
    if (parsed.inbox_revision < inbox_revision_) {
        return 0;
    }
    if (parsed.inbox_revision == inbox_revision_) {
        for (i = 0; i < parsed.message_count; ++i) {
            int idx = find_message_index(messages_, message_count_, parsed.messages[i].message_id);
            if (idx >= 0) {
                strncpy(messages_[idx].sender_name, parsed.messages[i].sender_name,
                        sizeof(messages_[idx].sender_name) - 1);
                strncpy(messages_[idx].sender_avatar_id, parsed.messages[i].sender_avatar_id,
                        sizeof(messages_[idx].sender_avatar_id) - 1);
                strncpy(messages_[idx].sender_card_background_id,
                        parsed.messages[i].sender_card_background_id,
                        sizeof(messages_[idx].sender_card_background_id) - 1);
            }
        }
        last_update_level_ = 1;
        connecting_ = 0;
        connecting_wait_index_ = -1;
        return 1;
    }
    for (i = 0; i < parsed.message_count; ++i) {
        int idx = find_message_index(messages_, message_count_, parsed.messages[i].message_id);
        if (idx >= 0) {
            copy_message(&messages_[idx], &parsed.messages[i]);
        } else if (message_count_ < kMaxMessages) {
            copy_message(&messages_[message_count_], &parsed.messages[i]);
            message_count_ += 1;
        }
    }
    inbox_revision_ = parsed.inbox_revision;
    if (prior_selected[0] != 0) {
        int keep = find_message_index(messages_, message_count_, prior_selected);
        if (keep >= 0) {
            selected_index_ = keep;
            strncpy(selected_message_id_, prior_selected, sizeof(selected_message_id_) - 1);
        }
    }
    last_update_level_ = 1;
    connecting_ = 0;
    connecting_wait_index_ = -1;
    next_ordinary_fetch_ms_ = now_ms + kInboxFetchPeriodMs;
    (void)old_count;
    return 1;
}

int CarouselSession::deliver_inbox_worker_result(uint32_t worker_generation, int perform_ok,
                                                 int http_status, const char *body, int body_len,
                                                 int64_t now_ms)
{
    InboxParseResult parsed;
    if (worker_generation != 0 && worker_generation != session_generation_) {
        return 0;
    }
    if (!classify_inbox_http(perform_ok, http_status, body, body_len)) {
        pause_for_connectivity(now_ms);
        return 0;
    }
    if (!parse_inbox_snapshot(body, body_len, body_len, &parsed)) {
        pause_for_connectivity(now_ms);
        return 0;
    }
    return apply_parsed_snapshot(parsed, now_ms);
}

void CarouselSession::select_index(int index, int64_t now_ms)
{
    if (index < 0 || index >= message_count_) {
        return;
    }
    if (index != selected_index_) {
        clear_media_buffers();
        playback_ = kPlaybackIdle;
        transport_ = kTransportPlay;
    }
    selected_index_ = index;
    strncpy(selected_message_id_, messages_[index].message_id, sizeof(selected_message_id_) - 1);
    selection_settled_ms_ = now_ms;
    pending_last_viewed_[0] = 0;
    last_viewed_due_ms_ = now_ms + kLastViewedCoalesceMs;
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
        strncpy(pending_last_viewed_, selected_message_id_, sizeof(pending_last_viewed_) - 1);
    }
}

void CarouselSession::advance_to(int64_t now_ms)
{
    static const int kWaitMs[] = {1000, 2000, 4000, 30000};
    maybe_emit_last_viewed(now_ms);
    if (connecting_ && wifi_up_ && now_ms >= connecting_retry_due_ms_) {
        if (connecting_wait_index_ < 0) {
            connecting_wait_index_ = 0;
        } else if (connecting_wait_index_ < 3) {
            connecting_wait_index_ += 1;
        }
        connecting_retry_due_ms_ = now_ms + kWaitMs[connecting_wait_index_ < 4 ? connecting_wait_index_ : 3];
        next_recovery_fetch_ms_ = now_ms;
    }
    if (cant_play_until_ms_ > 0 && now_ms >= cant_play_until_ms_) {
        cant_play_until_ms_ = 0;
        if (playback_ == kPlaybackCantPlay) {
            playback_ = kPlaybackPaused;
        }
    }
}

void CarouselSession::update_transport_for_playback()
{
    if (playback_ == kPlaybackPlaying) {
        transport_ = kTransportPause;
    } else if (playback_ == kPlaybackPaused || playback_ == kPlaybackBuffering ||
               playback_ == kPlaybackIdle) {
        transport_ = kTransportPlay;
    }
}

int CarouselSession::toggle_transport(int64_t now_ms)
{
    if (!wifi_up_ || connecting_) {
        return 0;
    }
    if (playback_ == kPlaybackPlaying) {
        playback_ = kPlaybackPaused;
        update_transport_for_playback();
        return 1;
    }
    if (selected_index_ < 0) {
        return 0;
    }
    if (audio_buffered_ < (int)kAudioPrefixBytes &&
        message_at(selected_index_)->audio.byte_count > kAudioPrefixBytes) {
        playback_ = kPlaybackBuffering;
    } else if (audio_buffered_ > 0) {
        playback_ = kPlaybackPlaying;
        sketch_visible_ = message_at(selected_index_)->sketch.present;
        portrait_hidden_ = sketch_visible_;
    } else {
        playback_ = kPlaybackBuffering;
    }
    update_transport_for_playback();
    (void)now_ms;
    return 1;
}

int CarouselSession::deliver_audio_chunk(const uint8_t *bytes, int len, int full_asset, int hash_matches,
                                         int64_t now_ms)
{
    if (!wifi_up_ || connecting_) {
        clear_media_buffers();
        return 0;
    }
    if (bytes == 0 || len <= 0) {
        return 0;
    }
    audio_buffered_ += len;
    if (full_asset) {
        if (!hash_matches) {
            playback_ = kPlaybackCantPlay;
            cant_play_until_ms_ = now_ms + kCantPlayMs;
            update_transport_for_playback();
            return 0;
        }
        audio_hash_ok_ = 1;
    }
    if (playback_ == kPlaybackBuffering && prefix_play_ready()) {
        playback_ = kPlaybackPlaying;
        sketch_visible_ = message_at(selected_index_)->sketch.present;
        portrait_hidden_ = sketch_visible_;
        update_transport_for_playback();
    }
    if (playback_ == kPlaybackPlaying && selected_index_ >= 0) {
        const InboxMessage *msg = message_at(selected_index_);
        if (msg->audio.byte_count > 0 && msg->duration_ms > 0) {
            play_head_ms_ += (int)((int64_t)len * msg->duration_ms / msg->audio.byte_count);
        }
    }
    return 1;
}

int CarouselSession::deliver_sketch_chunk(const uint8_t *bytes, int len, int64_t now_ms)
{
    const InboxMessage *msg;
    if (!wifi_up_ || connecting_ || bytes == 0 || len <= 0 || selected_index_ < 0) {
        return 0;
    }
    msg = message_at(selected_index_);
    if (msg == 0 || !msg->sketch.present) {
        return 0;
    }
    if (sketch_bytes_len_ + len > kSketchBufferCap ||
        sketch_bytes_len_ + len > (int)msg->sketch.byte_count) {
        return 0;
    }
    memcpy(sketch_bytes_ + sketch_bytes_len_, bytes, len);
    sketch_bytes_len_ += len;
    sketch_buffered_ = sketch_bytes_len_;
    if (sketch_bytes_len_ % 8 != 0) {
        return 1;
    }
    if (!flsk2_validate(sketch_bytes_, (size_t)sketch_bytes_len_, 0)) {
        return 0;
    }
    flsk2_paint_prefix(sketch_pixels_, kSketchPixels, sketch_bytes_, (size_t)sketch_bytes_len_,
                       (uint32_t)play_head_ms_, &sketch_rendered_);
    if (playback_ == kPlaybackPlaying && sketch_rendered_ > 0) {
        sketch_visible_ = 1;
        portrait_hidden_ = 1;
    }
    (void)now_ms;
    return 1;
}

void CarouselSession::on_mute(int64_t now_ms)
{
    clear_media_buffers();
    playback_ = kPlaybackIdle;
    transport_ = kTransportHidden;
    connecting_ = 0;
    settings_pause_active_ = 0;
    (void)now_ms;
}

void CarouselSession::pause_for_settings(int64_t now_ms)
{
    if (playback_ == kPlaybackPlaying) {
        playback_ = kPlaybackPaused;
        update_transport_for_playback();
    }
    settings_pause_active_ = 1;
    (void)now_ms;
}

void CarouselSession::note_refresh_success(int64_t now_ms)
{
    if (connecting_) {
        next_recovery_fetch_ms_ = now_ms;
    }
}

int CarouselSession::message_count() const { return message_count_; }
int CarouselSession::selected_index() const { return selected_index_; }
const char *CarouselSession::selected_message_id() const { return selected_message_id_; }

const InboxMessage *CarouselSession::message_at(int index) const
{
    if (index < 0 || index >= message_count_) {
        return 0;
    }
    return &messages_[index];
}

uint32_t CarouselSession::inbox_revision() const { return inbox_revision_; }

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

TransportState CarouselSession::transport_state() const { return transport_; }
PlaybackPhase CarouselSession::playback_phase() const { return playback_; }

int CarouselSession::play_disabled() const { return (!wifi_up_ || connecting_) ? 1 : 0; }
int CarouselSession::sketch_visible() const { return sketch_visible_; }
int CarouselSession::portrait_hidden() const { return portrait_hidden_; }
int CarouselSession::unread_badge_shown() const { return 0; }
int CarouselSession::last_update_level() const { return last_update_level_; }

int CarouselSession::inbox_fetch_due(int64_t now_ms) const
{
    if (!wifi_up_ || connecting_) {
        return 0;
    }
    return now_ms >= next_ordinary_fetch_ms_ ? 1 : 0;
}

int CarouselSession::recovery_fetch_due(int64_t now_ms) const
{
    if (!wifi_up_ || !connecting_) {
        return 0;
    }
    return now_ms >= next_recovery_fetch_ms_ ? 1 : 0;
}

const char *CarouselSession::pending_last_viewed_message_id() const { return pending_last_viewed_; }

int CarouselSession::last_viewed_write_due(int64_t now_ms) const
{
    return pending_last_viewed_[0] != 0 && now_ms >= last_viewed_due_ms_ ? 1 : 0;
}

int CarouselSession::audio_bytes_buffered() const { return audio_buffered_; }
int CarouselSession::prefix_play_ready() const
{
    if (selected_index_ < 0) {
        return 0;
    }
    const InboxMessage *msg = message_at(selected_index_);
    if (msg == 0) {
        return 0;
    }
    if (msg->audio.byte_count <= kAudioPrefixBytes) {
        return audio_buffered_ >= (int)msg->audio.byte_count;
    }
    return audio_buffered_ >= (int)kAudioPrefixBytes;
}

uint16_t CarouselSession::sketch_points_rendered() const { return sketch_rendered_; }

int CarouselSession::play_head_ms() const { return play_head_ms_; }

int CarouselSession::settings_pause_active() const { return settings_pause_active_; }

} /* namespace fl_product_inbox */
