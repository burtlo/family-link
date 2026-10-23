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
    int bind_payload(const FrozenPayload &payload, const uint8_t *audio, const uint8_t *sketch);
    void begin_submit(int64_t now_ms);
    void request_cancel();
    void advance_to(int64_t now_ms);

    int active() const;
    SendUiPhase ui_phase() const;
    SendTerminal terminal() const;
    const char *server_message_id() const;
    const char *client_message_id() const;
    int inbox_fetch_may_proceed() const;
    int part_size_bytes() const;
    int has_pending_http() const;
    int prepare_http_action(HttpAction *action, int64_t now_ms);
    int apply_transport_result(const HttpAction &action, const SendTransportResult &result, int64_t now_ms);

    int audio_part_sequence() const;
    int sketch_part_sequence() const;
    int trace_line(char *out, int out_cap) const;

private:
    void set_ui(SendUiPhase phase);
    void finish_terminal(SendTerminal terminal, SendUiPhase ui, int64_t now_ms);
    void schedule_part_retry(int64_t now_ms);
    void apply_com013(Com013Class com, int64_t now_ms);
    int part_bytes_for(uint32_t offset, uint32_t total, uint32_t *length_out) const;
    int build_part_path(char *path, int cap, const char *kind, uint32_t sequence) const;

    FrozenPayload payload_;
    const uint8_t *audio_;
    const uint8_t *sketch_;
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
    int64_t sent_until_ms_;
    int64_t next_action_ms_;
    int part_immediate_retry_left_;
    int part_retry_tier_;
    int cancel_requested_;
    int complete_response_lost_;
    char json_body_[kHttpBodyCap];
};

} /* namespace fl_product_send */

#endif
