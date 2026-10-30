#include "product_login_service.hpp"

#ifdef ESP_PLATFORM
#include "product_login_http_worker.hpp"
#endif
#include <algorithm>
#include <cstring>

namespace product_ui
{

LoginService &loginService()
{
    static LoginService service;
    return service;
}

LoginService::LoginService(LoginTransport transport) : transport_(transport)
{
#ifdef ESP_PLATFORM
    if (!transport_.start) transport_.start = product_login_http::worker_start;
    if (!transport_.advance) transport_.advance = product_login_http::service;
    if (!transport_.mute) transport_.mute = product_login_http::on_mute;
#endif
}

void LoginService::boot(int64_t now, bool wifiUp)
{
    if (booted_) return;
    booted_ = true;
    now_ = now;
    wifi_up_ = wifiUp;
    session_.boot(now, wifiUp);
}

void LoginService::scheduleWifi(bool up, int disconnectReason)
{
    (void)disconnectReason;
    if (!up) wifi_down_seen_.store(true);
    scheduled_wifi_.store(up ? 1 : 0);
}

void LoginService::scheduleWifiDriverFailed()
{
    scheduleWifi(false);
}

void LoginService::flushConnectivity(int64_t now)
{
    if (!booted_) boot(now);
    const int scheduled = scheduled_wifi_.exchange(-1);
    if (wifi_down_seen_.exchange(false) && wifi_up_) {
        wifi_up_ = false;
        session_.set_wifi(false, now);
    }
    if (scheduled < 0) return;
    wifi_up_ = scheduled != 0;
    session_.set_wifi(wifi_up_, now);
}

void LoginService::advance(int64_t now)
{
    if (!booted_) boot(now);
    now_ = now;
    if (wifi_up_ && !started_ && transport_.start) {
        started_ = transport_.start(&session_);
        if (started_ && !session_.roster_inflight() &&
            session_.stage() == fl_product_login::kStageConnecting) {
            session_.begin_roster_request(now);
        }
    }
    // Preserve the old bridge's bounded session-clock catch-up.
    session_.advance_to(std::min(now, session_.now_ms() + 250));
    session_.drain();
    if (started_ && transport_.advance) transport_.advance(&session_, now);
    reconcileFocus();
}

void LoginService::signOut(int64_t now)
{
    if (transport_.mute) transport_.mute();
    session_.mute(now);
}

void LoginService::refreshHangout(int64_t now)
{
    if (transport_.mute) transport_.mute();
    if (session_.stage() == fl_product_login::kStageMuted) session_.unmute(now);
    else session_.request_roster(now);
}

void LoginService::noteActivity(int64_t now)
{
    session_.note_user_activity(now);
}

void LoginService::noteActivity()
{
    noteActivity(now_);
}

bool LoginService::focusUser(const char *userId)
{
    if (!userId || session_.stage() != fl_product_login::kStageRoster || session_.roster_updating()) return false;
    for (int i = 0; i < session_.user_count(); ++i) {
        if (std::strcmp(session_.user_at(i), userId) != 0) continue;
        std::strncpy(focus_, userId, sizeof(focus_) - 1);
        focus_index_ = i;
        noteActivity();
        return true;
    }
    return false;
}

void LoginService::reconcileFocus()
{
    // Keep the selection while connectivity or mute temporarily removes cards.
    // Reconcile only against a valid roster; removed identities choose the
    // nearest preceding position, or the first remaining card.
    if (session_.stage() != fl_product_login::kStageRoster) return;
    const int count = session_.user_count();
    for (int i = 0; i < count; ++i) {
        if (std::strcmp(session_.user_at(i), focus_) == 0) {
            focus_index_ = i;
            return;
        }
    }
    if (count <= 0) { focus_[0] = '\0'; focus_index_ = -1; return; }
    focus_index_ = std::max(0, std::min(focus_index_ - 1, count - 1));
    std::strncpy(focus_, session_.user_at(focus_index_), sizeof(focus_) - 1);
}

bool LoginService::beginPin(const char *userId)
{
    if (session_.pin_phase() != fl_product_login::kPinPhaseIdle || session_.signed_in() ||
        !focusUser(userId)) return false;
    session_.select_pin_user(userId, now_);
    return session_.pin_phase() == fl_product_login::kPinPhaseEntry;
}

bool LoginService::enterPinDigit(char digit)
{
    if (pinSnapshot().place != PinSnapshot::Place::PinEntry) return false;
    return session_.enter_pin_digit(digit, now_);
}

void LoginService::pinBoot()
{
    session_.press_boot(now_);
}

PinSnapshot LoginService::pinSnapshot() const
{
    PinSnapshot result;
    const int phase = session_.pin_phase();
    if (phase == fl_product_login::kPinPhaseCooldown) result.place = PinSnapshot::Place::Cooldown;
    else if (session_.signed_in()) result.place = PinSnapshot::Place::Inbox;
    else if (phase != fl_product_login::kPinPhaseIdle) result.place = PinSnapshot::Place::PinEntry;
    std::strncpy(result.user, session_.focus_id(), sizeof(result.user) - 1);
    for (int i = 0; i < session_.user_count(); ++i) {
        if (std::strcmp(session_.user_at(i), result.user) == 0) {
            std::strncpy(result.name, session_.user_display_name_at(i), sizeof(result.name) - 1);
            break;
        }
    }
    result.digits = session_.digit_len();
    result.seconds = (session_.cooldown_remaining_ms() + 999) / 1000;
    result.checking = phase == fl_product_login::kPinPhaseChecking;
    result.wrong = phase == fl_product_login::kPinPhaseWrong;
    result.keypadEnabled = result.place == PinSnapshot::Place::PinEntry && session_.keypad_open();
    result.connectionStatus = session_.pin_lower_is_status();
    result.wifi = wifi_up_;
    result.dotPhase = static_cast<int>((now_ / 400) % 3);
    return result;
}

LoginSnapshot LoginService::snapshot() const
{
    LoginSnapshot result;
    result.known = session_.hangout_known();
    result.updating = session_.roster_updating();
    result.muted = session_.stage() == fl_product_login::kStageMuted;
    result.dotPhase = static_cast<int>((now_ / 400) % 3);
    result.count = std::min(session_.user_count(), static_cast<int>(fl_product_login::kMaxUsers));
    std::strncpy(result.focus, focus_[0] ? focus_ : session_.focus_id(), sizeof(result.focus) - 1);
    for (int i = 0; i < result.count; ++i) {
        auto &user = result.users[i];
        std::strncpy(user.id, session_.user_at(i), sizeof(user.id) - 1);
        std::strncpy(user.display_name, session_.user_display_name_at(i), sizeof(user.display_name) - 1);
        std::strncpy(user.avatar_id, session_.user_avatar_id_at(i), sizeof(user.avatar_id) - 1);
        std::strncpy(user.card_background_id, session_.user_card_background_at(i), sizeof(user.card_background_id) - 1);
    }
    if (!wifi_up_ || session_.stage() == fl_product_login::kStageWifiUnavailable) {
        result.body = LoginSnapshot::Body::NoWifi;
    } else if (session_.stage() == fl_product_login::kStageRoster && result.count > 0) {
        result.body = LoginSnapshot::Body::Roster;
    } else if (result.known && result.count == 0 && !result.updating &&
               session_.stage() != fl_product_login::kStageConnecting) {
        result.body = LoginSnapshot::Body::Empty;
    } else {
        result.body = LoginSnapshot::Body::Connecting;
    }
    return result;
}

const fl_product_login::SessionWriter &LoginService::session() const { return session_; }
bool LoginService::wifiUp() const { return wifi_up_; }

} // namespace product_ui
