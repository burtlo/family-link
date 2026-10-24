#ifndef FL_PRODUCT_INBOX_CAROUSEL_HPP
#define FL_PRODUCT_INBOX_CAROUSEL_HPP

#include "fl_product_inbox_p1_wire.hpp"
#include "fl_product_inbox_types.hpp"

namespace fl_product_inbox {

class CarouselSession {
public:
    CarouselSession();
    ~CarouselSession();

    CarouselSession(const CarouselSession &) = delete;
    CarouselSession &operator=(const CarouselSession &) = delete;

    void boot(int64_t now_ms);
    void set_wifi(bool up, int64_t now_ms);
    void set_session_generation(uint32_t generation);
    int deliver_inbox_worker_result(uint32_t worker_generation, int perform_ok, int http_status,
                                    const char *body, int body_len, int64_t now_ms);
    void select_index(int index, int64_t now_ms);
    void advance_to(int64_t now_ms);
    int toggle_transport(int64_t now_ms);
    int deliver_audio_chunk(uint32_t operation_id, const char *message_id, const uint8_t *bytes, int len,
                            int full_asset, int hash_matches, int64_t now_ms);
    int deliver_sketch_chunk(uint32_t operation_id, const char *message_id, const uint8_t *bytes, int len,
                             int64_t now_ms);
    void on_mute(int64_t now_ms);
    void note_refresh_success(int64_t now_ms);
    void note_refresh_failure(int64_t now_ms);
    void pause_for_settings(int64_t now_ms);
    void clear_settings_pause(int64_t now_ms);
    void pause_for_record(int64_t now_ms);

    int message_count() const;
    int selected_index() const;
    const char *selected_message_id() const;
    const InboxMessage *message_at(int index) const;
    uint32_t inbox_revision() const;
    int has_valid_inbox() const;
    ConnectivitySlot connectivity_slot() const;
    TransportState transport_state() const;
    PlaybackPhase playback_phase() const;
    uint32_t playback_operation_id() const;
    int play_disabled() const;
    int sketch_visible() const;
    int portrait_hidden() const;
    int unread_badge_shown() const;
    int last_update_level() const;
    int inbox_fetch_due(int64_t now_ms) const;
    int recovery_fetch_due(int64_t now_ms) const;
    int inbox_fetch_armed() const;
    int ui_visible() const;
    void set_ui_visible(int visible);
    /* 0 while Wi-Fi is down or connecting is set. Audio and sketch only. */
    int media_fetch_allowed() const;
    int recovery_pulse_pending() const;
    void clear_recovery_pulse();
    void arm_inbox_fetch();
    void consume_inbox_arm();
    /* A snapshot for some other in-flight GET must not drop a pending arm. */
    void retain_inbox_arm_on_snapshot(int retain);
    /* Moves the ordinary or recovery clock after a GET was accepted onto the worker queue. */
    void acknowledge_inbox_fetch(int64_t now_ms);
    const char *pending_last_viewed_message_id() const;
    int last_viewed_write_due(int64_t now_ms) const;
    uint32_t session_generation() const;
    int sketch_bytes_stored() const;
    /* parsed is caller-owned. The service tick passes a worker heap block and does not
     * declare InboxParseResult as a local. */
    int apply_inbox_parsed(uint32_t worker_generation, const InboxParseResult *parsed, int64_t now_ms);
    int apply_audio_fetch_result(uint32_t generation, uint32_t operation_id, const char *message_id,
                                 int perform_ok, int http_status, const uint8_t *bytes, int len,
                                 int full_asset, int hash_matches, int64_t now_ms);
    int apply_sketch_fetch_result(uint32_t generation, uint32_t operation_id, const char *message_id,
                                  int perform_ok, int http_status, const uint8_t *bytes, int len,
                                  int full_asset, int hash_matches, int64_t now_ms);
    int audio_bytes_buffered() const;
    int prefix_play_ready() const;
    uint16_t sketch_points_rendered() const;
    int play_head_ms() const;
    int settings_pause_active() const;
    const uint16_t *selected_sketch_pixels() const;
    int message_sketch_len(int index) const;
    int play_head_ms_at(int index) const;
    int message_completed_at(int index) const;
    /* Paints one message into the caller buffer from that message's saved
     * sketch bytes. Does not allocate and does not replace the selected frame.
     * Returns 1 when the paint contains ink. */
    int paint_message_sketch(int index, uint16_t *pixels, int pixel_count) const;

private:
    struct MessageRuntime {
        int play_head_ms;
        int completed;
        int paused;
        int audio_buffered;
        uint8_t *sketch_bytes;
        int sketch_len;
    };

    void reset_fields(int64_t now_ms);
    void release_sketch_buffers();
    void clear_runtime_slot(MessageRuntime *slot);
    void pause_active_playback();
    void begin_connecting(int64_t now_ms);
    void cancel_playback_operation();
    void assign_playback_operation(const char *message_id);
    int apply_parsed_snapshot(const InboxParseResult &parsed, int64_t now_ms);
    void apply_higher_revision(const InboxParseResult &parsed, int64_t now_ms);
    void sync_phase_from_selected();
    void repaint_selected_sketch();
    void schedule_last_viewed(int64_t now_ms);
    void maybe_emit_last_viewed(int64_t now_ms);
    void release_snapshot_arm();
    void update_transport_for_playback();
    MessageRuntime *selected_runtime();
    const MessageRuntime *selected_runtime() const;

    int wifi_up_;
    int ui_visible_;
    int has_valid_inbox_;
    uint32_t inbox_revision_;
    uint32_t session_generation_;
    int message_count_;
    InboxMessage messages_[kMaxMessages];
    MessageRuntime runtime_[kMaxMessages];
    int selected_index_;
    char selected_message_id_[kMaxMessageId];
    int64_t selection_settled_ms_;
    char pending_last_viewed_[kMaxMessageId];
    int64_t last_viewed_due_ms_;
    int64_t next_ordinary_fetch_ms_;
    int64_t next_recovery_fetch_ms_;
    int inbox_fetch_armed_;
    int snapshot_keeps_arm_;
    int recovery_pulse_;
    int connecting_;
    int connecting_wait_index_;
    int64_t connecting_retry_due_ms_;
    int last_update_level_;
    TransportState transport_;
    PlaybackPhase playback_;
    uint32_t playback_operation_id_;
    uint32_t next_playback_operation_;
    char playback_message_id_[kMaxMessageId];
    int64_t cant_play_until_ms_;
    int sketch_visible_;
    int portrait_hidden_;
    uint16_t sketch_rendered_;
    uint16_t sketch_pixels_[kSketchPixels];
    int settings_pause_active_;
};

} /* namespace fl_product_inbox */

#endif
