#ifndef FL_PRODUCT_INBOX_TYPES_HPP
#define FL_PRODUCT_INBOX_TYPES_HPP

#include <stdint.h>

namespace fl_product_inbox
{

enum {
    kMaxMessages = 64,
    kMaxMessageId = 48,
    kMaxUserId = 24,
    kMaxName = 48,
    kMaxAvatarId = 24,
    kMaxCardBgId = 24,
    kMaxSha256 = 65,
    kInboxFetchPeriodMs = 60000,
    kLastViewedCoalesceMs = 1000,
    kAudioPrefixBytes = 32000,
    kCantPlayMs = 2000,
    kSketchGrid = 120,
    kSketchPixels = 120 * 120,
    kConnectingTimeoutMs = 3000,
    kMediaBufferCap = 5760000,
    kSketchBufferCap = 4096
};

enum ConnectivitySlot {
    kSlotNone = 0,
    kSlotConnecting = 1,
    kSlotNoWifi = 2
};

enum TransportState {
    kTransportHidden = 0,
    kTransportPlay = 1,
    kTransportPause = 2
};

enum PlaybackPhase {
    kPlaybackIdle = 0,
    kPlaybackBuffering = 1,
    kPlaybackPlaying = 2,
    kPlaybackPaused = 3,
    kPlaybackCantPlay = 4
};

struct MediaDescriptor {
    char format_id[16];
    uint32_t byte_count;
    char sha256[kMaxSha256];
    int present;
};

struct InboxMessage {
    char message_id[kMaxMessageId];
    char sender_user_id[kMaxUserId];
    char sender_name[kMaxName];
    char sender_avatar_id[kMaxAvatarId];
    char sender_card_background_id[kMaxCardBgId];
    uint32_t duration_ms;
    MediaDescriptor audio;
    MediaDescriptor sketch;
};

struct SenderPresentation {
    char display_name[kMaxName];
    char avatar_id[kMaxAvatarId];
    char card_background_id[kMaxCardBgId];
    int use_initials_fallback;
    int use_card_fallback;
};

} /* namespace fl_product_inbox */

#endif
