#ifndef FL_PRODUCT_INBOX_WIRE_HPP
#define FL_PRODUCT_INBOX_WIRE_HPP

#include "fl_product_inbox_carousel.hpp"

namespace fl_product_inbox
{

enum InboxWireKind {
    kInboxWireNone = 0,
    kInboxWireInbox = 1,
    kInboxWireLastViewed = 2,
    kInboxWireAudio = 3,
    kInboxWireSketch = 4
};

/* One queued exchange. The device copies this onto the worker queue. */
struct InboxWireJob {
    int kind;
    char method[8];
    char path[96];
    char body[256];
    int body_len;
    char range_header[40];
    char operation_uuid[40];
    char message_id[kMaxMessageId];
    char sha256[kMaxSha256];
    uint32_t generation;
    uint32_t operation_id;
    uint32_t range_start;
    uint32_t asset_bytes;
    int timeout_ms;
};

struct InboxWireSlots {
    int inbox;
    int last_viewed;
    int audio;
    int sketch;
    InboxWireJob inbox_job;
    InboxWireJob last_viewed_job;
    InboxWireJob audio_job;
    InboxWireJob sketch_job;
};

/* Host-testable planner. HTTP stays on the device worker. At most one job of each kind
 * is in flight. ordinary_due and recovery_due are sampled before advance_to. */
class InboxWirePlanner
{
  public:
    InboxWirePlanner();
    void reset();
    void collect(CarouselSession &session, InboxWireSlots *out, int64_t now_ms, int ordinary_due,
                 int recovery_due);
    void accept_queued(CarouselSession &session, int kind, int64_t now_ms);
    void refuse_queued(CarouselSession &session, int kind, int64_t now_ms);
    /* 1 when an audio or sketch job already queued must be dropped before HTTP. */
    static int media_job_cancelled(const CarouselSession &session, int kind);
    void apply_inbox(CarouselSession &session, uint32_t generation, int perform_ok, int http_status,
                     const char *body, int body_len, int64_t now_ms);
    void apply_inbox_parsed(CarouselSession &session, uint32_t generation, const InboxParseResult *parsed,
                            int64_t now_ms);
    void apply_last_viewed(CarouselSession &session, uint32_t generation, const char *body, int body_len,
                           int perform_ok, int http_status, int64_t now_ms);
    void apply_audio(CarouselSession &session, uint32_t generation, uint32_t operation_id,
                     const char *message_id, int perform_ok, int http_status, const uint8_t *bytes, int len,
                     int full_asset, int hash_matches, int terminal, int64_t now_ms);
    void apply_sketch(CarouselSession &session, uint32_t generation, uint32_t operation_id,
                      const char *message_id, int perform_ok, int http_status, const uint8_t *bytes, int len,
                      int full_asset, int hash_matches, int terminal, int64_t now_ms);

  private:
    void fill_inbox_job(CarouselSession &session, InboxWireJob *job);
    int fill_last_viewed_job(CarouselSession &session, InboxWireJob *job, int64_t now_ms);
    int fill_media_job(CarouselSession &session, InboxWireJob *job, int sketch, int64_t now_ms);

    int inbox_inflight_;
    int last_viewed_inflight_;
    int audio_inflight_;
    int sketch_inflight_;
    int took_inbox_arm_;
    int64_t inbox_quiet_until_ms_;
    int64_t last_viewed_quiet_until_ms_;
    int64_t audio_quiet_until_ms_;
    int64_t sketch_quiet_until_ms_;

    int have_last_viewed_body_;
    uint64_t next_last_viewed_op_;
    char last_viewed_uuid_[40];
    char last_viewed_message_[kMaxMessageId];
    char last_viewed_body_[256];
    int last_viewed_body_len_;
    int64_t last_viewed_retain_until_ms_;
    char last_viewed_acknowledged_[kMaxMessageId];
    char last_viewed_suppress_[kMaxMessageId];
};

} /* namespace fl_product_inbox */

#endif
