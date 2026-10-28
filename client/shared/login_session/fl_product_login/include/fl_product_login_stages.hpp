#ifndef FL_PRODUCT_LOGIN_STAGES_HPP
#define FL_PRODUCT_LOGIN_STAGES_HPP

/* Single header for login stage tokens. LAB-08 links this header.
   No other module may define these token constants. */

namespace fl_product_login
{

enum Stage {
    kStageWifiUnavailable = 1,
    kStageConnecting = 2,
    kStageRoster = 3,
    kStageEmptyHangout = 4,
    kStageMuted = 5,
    kStageSignedIn = 6,
    kStageSignedOut = 7
};

constexpr char kTokenWifiUnavailable[] = "wifi_unavailable";
constexpr char kTokenConnecting[] = "connecting";
constexpr char kTokenRoster[] = "roster";
constexpr char kTokenEmptyHangout[] = "empty_hangout";
constexpr char kTokenMuted[] = "muted";
constexpr char kTokenSignedIn[] = "signed_in";
constexpr char kTokenSignedOut[] = "signed_out";

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
        case kStageSignedIn:
            return kTokenSignedIn;
        case kStageSignedOut:
            return kTokenSignedOut;
        default:
            return "";
    }
}

} /* namespace fl_product_login */

#endif
