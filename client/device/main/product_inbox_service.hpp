#pragma once

#include "fl_product_inbox_p1_wire.hpp"
#include "product_inbox_http_worker.hpp"
#include <cstdint>

namespace product_ui
{

struct InboxTransport {
    bool (*start)() = nullptr;
    bool (*submit)(const product_inbox_http::Job *) = nullptr;
    const product_inbox_http::Result *(*poll)() = nullptr;
};

struct InboxSnapshot {
    enum class Body { Connecting,
                      NoWifi,
                      Empty,
                      Cards } body = Body::Connecting;
    int count = 0;
    int selectedIndex = -1;
    uint32_t revision = 0;
    int dotPhase = 0;
};

// Owns metadata only. A worker transfers a PSRAM metadata block; there are no
// pixel surfaces, media bodies, controller pointers, or LVGL dependencies.
class InboxService
{
  public:
    explicit InboxService(InboxTransport transport = {});
    ~InboxService();
    InboxService(const InboxService &) = delete;
    InboxService &operator=(const InboxService &) = delete;
    void setSession(const char *user, const char *bearer, uint32_t generation,
                    bool wifi, bool credentialReady, int64_t now);
    // Recording holds a new inbox fetch. An in-flight fetch still completes.
    void setFetchHeld(bool held)
    {
        fetch_held_ = held;
    }
    void signOut();
    void noteSessionUnready();
    void setMediaSink(void (*sink)(void *, const product_inbox_http::Result &), void *context)
    {
        media_sink_ = sink;
        media_context_ = context;
    }
    // Host tests replace std::free so an unknown result kind can be counted.
    void setResultReleaser(void (*release)(void *))
    {
        release_ = release;
    }
    bool submitExternal(const product_inbox_http::Job *job);
    void advance(int64_t now);
    void requestRefresh();
    bool selectIndex(int index, const char *expectedId = nullptr);
    bool retainSelection(const char *messageId);
    InboxSnapshot snapshot() const;
    const fl_product_inbox::InboxMessage *messageAt(int index) const;
    const char *selectedMessageId() const;
    uint32_t generation() const
    {
        return generation_;
    }

  private:
    bool applyMetadata(fl_product_inbox::InboxMetadataHeader *block);
    void consume(const product_inbox_http::Result &result);
    void fetchFailure();
    bool submitMetadata();
    bool submitLastViewed();
    int find(const fl_product_inbox::InboxMetadataHeader *block, const char *id) const;
    void selectionChanged();
    uint32_t nextOperation();
    InboxTransport transport_;
    fl_product_inbox::InboxMetadataHeader *metadata_ = nullptr;
    char user_[24]{};
    char bearer_[48]{};
    uint32_t auth_generation_ = 0;
    uint32_t generation_ = 0;
    uint32_t next_operation_ = 0;
    uint32_t fetch_operation_ = 0;
    uint32_t write_operation_ = 0;
    int selected_ = -1;
    int retry_index_ = 0;
    int64_t now_ = 0;
    int64_t fetch_due_ = 0;
    int64_t selection_due_ = 0;
    int64_t write_due_ = 0;
    int64_t write_created_ = 0;
    bool wifi_ = false;
    bool credential_ready_ = false;
    bool started_ = false;
    bool connecting_ = true;
    bool refresh_requested_ = true;
    bool fetch_held_ = false;
    bool write_pending_ = false;
    bool selection_pending_ = false;
    void (*media_sink_)(void *, const product_inbox_http::Result &) = nullptr;
    void *media_context_ = nullptr;
    void (*release_)(void *) = nullptr;
    char acknowledged_[fl_product_inbox::kMaxMessageId]{};
    char pending_message_[fl_product_inbox::kMaxMessageId]{};
    char operation_uuid_[40]{};
    char operation_prefix_[24]{};
    char write_body_[256]{};
    int write_body_len_ = 0;
};

} // namespace product_ui
