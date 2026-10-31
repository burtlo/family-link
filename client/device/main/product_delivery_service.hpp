#pragma once

#include "fl_product_send_stage.hpp"
#include "product_capture_service.hpp"
#include "product_message_target_service.hpp"

#include <cstdint>

namespace product_ui
{

struct DeliveryHttpResult {
    int perform_ok = 0;
    int http_status = 0;
    int body_len = 0;
    uint32_t seq = 0;
    char client_message_id[48]{};
    char body[4096]{};
};

struct DeliveryTransport {
    bool (*start)() = nullptr;
    bool (*submit)(const char *method, const char *path, const uint8_t *body, uint32_t body_len, int is_json,
                   const char *client_message_id, uint32_t seq, const char *bearer) = nullptr;
    const DeliveryHttpResult *(*poll)() = nullptr;
};

// Send journey and upload. Views read snapshots; they do not start workers.
class DeliveryService
{
  public:
    explicit DeliveryService(CaptureService &capture);
    void setTransport(DeliveryTransport transport);
    void setPlayback(void (*pause)(void *), const char *(*selected)(void *), void *context);
    void setSession(const char *user, const char *bearer, bool wifi, bool refreshFailed, uint32_t generation,
                    int64_t now);
    void noteSeed(uint32_t seed);
    void mirrorSelection(const MessageTargetSnapshot &snapshot);
    fl_product_send::SendStageResult apply(fl_product_send::SendIntent intent, int64_t now, bool signedIn);
    void advance(int64_t now);
    int place() const
    {
        return stage_.place();
    }
    bool showsSketch() const;
    bool showsFeedback() const;
    bool couldntRecord() const;
    bool sketchBoot() const;
    bool sketchRed() const;
    bool feedbackBoot() const;
    void header(char *title, int titleCap, char *subtitle, int subtitleCap) const;
    void body(char *out, int cap) const;
    const char *rememberedSelection() const;
    CaptureService &capture()
    {
        return capture_;
    }
    DeliveryHttpResult &polledSlot()
    {
        return polled_;
    }

  private:
    void fillHooks();
    void stageCaptureIdentity();
    void pumpHttp(int64_t now);
    void releaseTerminal();
    void adoptFinished(int64_t now);
    bool startPayload(int64_t now);
    static int readPart(void *context, int kind, uint32_t offset, uint8_t *dest, uint32_t length, uint32_t *read);
    static void pausePlayback(void *context, int64_t now);
    static const char *selectedMessage(void *context);
    static int canStartSecond(void *context);
    static void discardCapture(void *context);
    static void cancelCapture(void *context);
    static int finishTake(void *context);
    static int captureCanStart(void *context);
    static void beginWipe(void *context, const char *clientMessageId, int64_t now);
    static void beginReverse(void *context, int64_t now);
    static void beginCollapse(void *context, int64_t now);

    CaptureService &capture_;
    fl_product_send::SendJourney journey_;
    fl_product_send::SendStageReducer stage_;
    fl_product_send::SendStageHooks hooks_{};
    DeliveryTransport transport_{};
    void (*pause_)(void *) = nullptr;
    const char *(*selected_)(void *) = nullptr;
    void *playback_ = nullptr;
    char user_[fl_product_send::kMaxUserId]{};
    char bearer_[96]{};
    char media_id_[fl_product_send::kMaxClientMessageId]{};
    bool wifi_ = false;
    bool refresh_failed_ = false;
    bool seeded_ = false;
    bool http_started_ = false;
    bool begin_failed_ = false;
    int dot_phase_ = 0;
    int64_t dot_due_ = 0;
    int64_t now_ = 0;
    DeliveryHttpResult polled_{};
};

} // namespace product_ui
