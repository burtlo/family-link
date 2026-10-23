#ifndef FL_PRODUCT_SEND_OUTBOX_HPP
#define FL_PRODUCT_SEND_OUTBOX_HPP

#include "fl_product_send_types.hpp"

namespace fl_product_send {

struct SendTransportResult {
    int transport_ok;
    int response_lost;
    int http_status;
    const char *body;
    int body_len;
};

class SendOutbox {
public:
    SendOutbox();

    void reset();
    void set_wifi(int up);
    void set_refresh_failed(int failed);
    void set_upload_blocked(int blocked);
    void set_worker_generation(uint32_t generation);
    void set_signed_in_user(const char *user_id);
    int signed_in_matches_sender() const;
    int bind_payload(const FrozenPayload &payload, const uint8_t *audio, const uint8_t *sketch);
    /* Hash and send from a part reader. Does not keep a full-take image. */
    int bind_stored(const FrozenPayload &payload, const MediaPartReader &reader);
    void begin_submit(int64_t now_ms);
    void request_cancel();
    void leave_wait();
    int left_wait() const;
    void advance_to(int64_t now_ms);

    int active() const;
    SendUiPhase ui_phase() const;
    const char *body_label() const;
    SendTerminal terminal() const;
    const char *server_message_id() const;
    const char *client_message_id() const;
    int inbox_fetch_may_proceed() const;
    int part_size_bytes() const;
    int has_pending_http() const;
    int prepare_http_action(HttpAction *action, int64_t now_ms);
    int apply_transport_result(const HttpAction &action, const SendTransportResult &result, int64_t now_ms);
    /* Device worker: one part is in flight. A result for another id or seq is ignored. */
    void note_http_sent(uint32_t *seq_out);
    void cancel_http_sent();
    int http_inflight() const;
    int apply_http_result(const char *client_message_id, uint32_t seq, const HttpAction &action,
                          const SendTransportResult &result, int64_t now_ms);

    int audio_part_sequence() const;
    int sketch_part_sequence() const;
    int trace_line(char *out, int out_cap) const;

private:
    void set_ui(SendUiPhase phase);
    void finish_terminal(SendTerminal terminal, SendUiPhase ui, int64_t now_ms);
    void note_accepted(int64_t now_ms);
    void schedule_part_retry(int64_t now_ms);
    void apply_com013(Com013Class com, int64_t now_ms);
    int part_bytes_for(uint32_t offset, uint32_t total, uint32_t *length_out) const;
    int build_part_path(char *path, int cap, const char *kind, uint32_t sequence) const;
    int hash_stored(int kind, uint32_t total, char *hex_out);
    int fill_part_body(HttpAction *action, int kind, const uint8_t *fallback, uint32_t offset, uint32_t part_len);

    FrozenPayload payload_;
    const uint8_t *audio_;
    const uint8_t *sketch_;
    MediaPartReader reader_;
    uint8_t part_window_[kPartSizeBytes];
    int http_inflight_;
    uint32_t http_seq_;
    char server_message_id_[kMaxMessageId];
    char audio_sha_[kMaxSha256Hex];
    char sketch_sha_[kMaxSha256Hex];
    uint32_t audio_parts_total_;
    uint32_t sketch_parts_total_;
    uint32_t audio_parts_sent_;
    uint32_t sketch_parts_sent_;
    SendWireStep step_;
    SendUiPhase ui_;
    SendTerminal terminal_;
    int wifi_up_;
    int refresh_failed_;
    int upload_blocked_;
    uint32_t worker_generation_;
    int64_t hold_until_ms_;
    int64_t next_action_ms_;
    int part_immediate_retry_left_;
    int part_retry_tier_;
    int left_wait_;
    int complete_response_lost_;
    char signed_in_user_id_[kMaxUserId];
    char json_body_[kHttpBodyCap];
};

} /* namespace fl_product_send */

#endif
