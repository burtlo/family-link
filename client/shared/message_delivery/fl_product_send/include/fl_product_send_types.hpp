#ifndef FL_PRODUCT_SEND_TYPES_HPP
#define FL_PRODUCT_SEND_TYPES_HPP

#include <stdint.h>

namespace fl_product_send
{

enum {
    kPartSizeBytes = 16384,
    kMaxClientMessageId = 48,
    kMaxMessageId = 48,
    kMaxUserId = 24,
    kMaxName = 48,
    kMaxAvatarId = 24,
    kMaxCardBgId = 24,
    kMaxRecipients = 7,
    kMaxInboxSelectionId = 48,
    kSentHoldMs = 2000,
    kPartRetryImmediate = 0,
    kPartRetry1Ms = 1000,
    kPartRetry2Ms = 2000,
    kPartRetry4Ms = 4000,
    kPartRetry30Ms = 30000,
    kMaxSha256Hex = 65,
    kHttpBodyCap = 4096,
    kTraceLineCap = 256,
    kTestAudioCap = 65536,
    kTestSketchCap = 4096
};

enum SendUiPhase {
    kSendUiHidden = 0,
    kSendUiPicker,
    kSendUiPreparing,
    kSendUiSending,
    kSendUiSent,
    kSendUiCouldntSend,
    kSendUiQueuedWait
};

enum SendTerminal {
    kSendTerminalNone = 0,
    kSendTerminalAccepted,
    kSendTerminalDiscarded,
    kSendTerminalCancelled
};

enum Com013Class {
    kCom013Queue = 0,
    kCom013Discard,
    kCom013WaitMalformed
};

enum SendWireStep {
    kWireIdle = 0,
    kWireCreate,
    kWireAudioPart,
    kWireSketchPart,
    kWireComplete,
    kWireLookup
};

struct FrozenPayload {
    char client_message_id[kMaxClientMessageId];
    char sender_user_id[kMaxUserId];
    char recipient_user_ids[kMaxRecipients][kMaxUserId];
    int recipient_count;
    uint32_t audio_byte_count;
    uint32_t sketch_byte_count;
    uint32_t duration_ms;
    int sketch_present;
};

struct HttpAction {
    char method[8];
    char path[160];
    const uint8_t *body;
    uint32_t body_len;
    int is_json;
};

/* kind 1 is audio and kind 2 is sketch. A read fills dest with at most
 * kPartSizeBytes at offset within that stored object. */
struct MediaPartReader {
    void *ctx;
    int (*read_at)(void *ctx, int kind, uint32_t offset, uint8_t *dest, uint32_t len, uint32_t *out_read);
};

} /* namespace fl_product_send */

#endif
