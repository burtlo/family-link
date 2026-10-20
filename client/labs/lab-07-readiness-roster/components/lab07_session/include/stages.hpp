#ifndef LAB07_STAGES_HPP
#define LAB07_STAGES_HPP

/* Single header for login stage tokens. LAB-08 links this header.
   No other module may define these token constants. */

namespace lab07 {

enum Stage {
    kStageWifiUnavailable = 1,
    kStageConnecting = 2,
    kStageRoster = 3,
    kStageEmptyHangout = 4,
    kStageMuted = 5
};

constexpr char kTokenWifiUnavailable[] = "wifi_unavailable";
constexpr char kTokenConnecting[] = "connecting";
constexpr char kTokenRoster[] = "roster";
constexpr char kTokenEmptyHangout[] = "empty_hangout";
constexpr char kTokenMuted[] = "muted";

inline const char *stage_token(int stage)
{
    switch (stage) {
    case kStageWifiUnavailable:
        return kTokenWifiUnavailable;
    case kStageConnecting:
        return kTokenConnecting;
    case kStageRoster:
        return kTokenRoster;
    case kStageEmptyHangout:
        return kTokenEmptyHangout;
    case kStageMuted:
        return kTokenMuted;
    default:
        return "";
    }
}

} /* namespace lab07 */

#endif
