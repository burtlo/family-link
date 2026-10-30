#pragma once

#include "fl_product_login_session.hpp"
#include <atomic>

namespace product_ui
{

struct LoginSnapshot {
    enum class Body { NoWifi, Connecting, Empty, Roster } body = Body::NoWifi;
    bool known = false;
    bool updating = false;
    bool muted = false;
    int count = 0;
    int dotPhase = 0;
    char focus[fl_product_login::kMaxId]{};
    fl_product_login::UserId users[fl_product_login::kMaxUsers]{};
};

struct LoginTransport {
    bool (*start)(fl_product_login::SessionWriter *) = nullptr;
    void (*advance)(fl_product_login::SessionWriter *, int64_t) = nullptr;
    void (*mute)() = nullptr;
};

struct PinSnapshot {
    enum class Place { Roster, PinEntry, Cooldown, Inbox } place = Place::Roster;
    char user[fl_product_login::kMaxId]{};
    char name[32]{};
    int digits = 0;
    int seconds = 0;
    bool checking = false;
    bool wrong = false;
    bool keypadEnabled = false;
    bool connectionStatus = false;
    bool wifi = false;
    int dotPhase = 0;
};

// Runtime/service lifetime. Contains no LVGL objects or rendering dependency.
class LoginService
{
  public:
    explicit LoginService(LoginTransport transport = {});
    void boot(int64_t now, bool wifiUp = false);
    void scheduleWifi(bool up, int disconnectReason = 0);
    void scheduleWifiDriverFailed();
    void flushConnectivity(int64_t now);
    void advance(int64_t now);
    void signOut(int64_t now);
    void refreshHangout(int64_t now);
    void noteActivity(int64_t now);
    void noteActivity();
    bool focusUser(const char *userId);
    bool beginPin(const char *userId);
    bool enterPinDigit(char digit);
    void pinBoot();
    PinSnapshot pinSnapshot() const;
    LoginSnapshot snapshot() const;
    const fl_product_login::SessionWriter &session() const;
    bool wifiUp() const;

  private:
    void reconcileFocus();
    LoginTransport transport_;
    fl_product_login::SessionWriter session_;
    std::atomic<int> scheduled_wifi_{-1};
    // Set by any down event so a down+up pair within one tick is not lost.
    std::atomic<bool> wifi_down_seen_{false};
    bool wifi_up_ = false;
    bool started_ = false;
    bool booted_ = false;
    int64_t now_ = 0;
    char focus_[fl_product_login::kMaxId]{};
    int focus_index_ = -1;
};

LoginService &loginService();

} // namespace product_ui
