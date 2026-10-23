#ifndef FL_PRODUCT_INBOX_CAROUSEL_HPP
#define FL_PRODUCT_INBOX_CAROUSEL_HPP

#include "fl_product_inbox_p1_wire.hpp"
#include "fl_product_inbox_types.hpp"

namespace fl_product_inbox {

class CarouselSession {
public:
    CarouselSession();

    void boot(int64_t now_ms);
    void set_wifi(bool up, int64_t now_ms);
    void set_session_generation(uint32_t generation);
    int deliver_inbox_worker_result(uint32_t worker_generation, int perform_ok, int http_status,
                                    const char *body, int body_len, int64_t now_ms);
    void select_index(int index, int64_t now_ms);
    void advance_to(int64_t now_ms);
    int toggle_transport(int64_t now_ms);
    int deliver_audio_chunk(const uint8_t *bytes, int len, int full_asset, int hash_matches,
                            int64_t now_ms);
    int deliver_sketch_chunk(const uint8_t *bytes, int len, int64_t now_ms);
    void on_mute(int64_t now_ms);
    void note_refresh_success(int64_t now_ms);
    void pause_for_settings(int64_t now_ms);

    int message_count() const;
    int selected_index() const;
    const char *selected_message_id() const;
    const InboxMessage *message_at(int index) const;
    uint32_t inbox_revision() const;
    ConnectivitySlot connectivity_slot() const;
    TransportState transport_state() const;
    PlaybackPhase playback_phase() const;
    int play_disabled() const;
    int sketch_visible() const;
    int portrait_hidden() const;
    int unread_badge_shown() const;
    int last_update_level() const;
    int inbox_fetch_due(int64_t now_ms) const;
    int recovery_fetch_due(int64_t now_ms) const;
    const char *pending_last_viewed_message_id() const;
    int last_viewed_write_due(int64_t now_ms) const;
    int audio_bytes_buffered() const;
    int prefix_play_ready() const;
    uint16_t sketch_points_rendered() const;
    int play_head_ms() const;
    int settings_pause_active() const;

private:
    void clear_media_buffers();
    void pause_for_connectivity(int64_t now_ms);
    int apply_parsed_snapshot(const InboxParseResult &parsed, int64_t now_ms);
    void schedule_last_viewed(int64_t now_ms);
    void maybe_emit_last_viewed(int64_t now_ms);
    void update_transport_for_playback();

    int wifi_up_;
    int has_valid_inbox_;
    uint32_t inbox_revision_;
    uint32_t session_generation_;
    int message_count_;
    InboxMessage messages_[kMaxMessages];
    int selected_index_;
    char selected_message_id_[kMaxMessageId];
    int64_t selection_settled_ms_;
    char pending_last_viewed_[kMaxMessageId];
    int64_t last_viewed_due_ms_;
    int64_t next_ordinary_fetch_ms_;
    int64_t next_recovery_fetch_ms_;
    int connecting_;
    int connecting_wait_index_;
    int64_t connecting_retry_due_ms_;
    int last_update_level_;
    TransportState transport_;
    PlaybackPhase playback_;
    int64_t cant_play_until_ms_;
    int audio_buffered_;
    int audio_hash_ok_;
    int sketch_buffered_;
    int sketch_bytes_len_;
    uint8_t sketch_bytes_[kSketchBufferCap];
    int sketch_visible_;
    int portrait_hidden_;
    uint16_t sketch_rendered_;
    uint16_t sketch_pixels_[kSketchPixels];
    int play_head_ms_;
    int settings_pause_active_;
};

} /* namespace fl_product_inbox */

#endif
