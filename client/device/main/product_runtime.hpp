#pragma once

#include "product_login_view_controller.hpp"
#include "product_inbox_view_controller.hpp"
#include "product_message_target_view_controller.hpp"
#include "product_settings_view_controller.hpp"

namespace product_ui
{

struct RuntimePlatform {
    int (*muteLatched)() = nullptr;
    void (*blankDisplay)() = nullptr;
    void (*wakeDisplay)() = nullptr;
    bool (*takeBoot)() = nullptr;
    bool (*takeRed)() = nullptr;
};

struct RuntimeServices {
    InboxTransport inbox;
    SettingsTransport settings;
};

enum class RuntimeDestination { Login, PinEntry, Cooldown, Inbox, Settings, MessageTarget, Recording };
struct RuntimeNavigation {
    RuntimeDestination destination = RuntimeDestination::Login;
    uint32_t generation = 0;
    char user[fl_product_login::kMaxId]{};
    MessageRecipients recipients;
    bool animated = true;
    CoverDirection direction = CoverDirection::Forward;
};

class ProductRuntime
{
  public:
    ProductRuntime(RootViewController &root, LoginService &service, RuntimePlatform platform = {},
                   RuntimeServices services = {});
    ~ProductRuntime();
    bool start();
    void tick(int64_t now);
    LoginNavigation navigation();
    bool muted() const { return muted_; }
    InboxService &inboxService() { return inbox_; }
    SettingsService &settingsService() { return settings_; }
    MessageTargetService &targetService() { return targets_; }
    const MessageRecipients &recordingRecipients() const { return recording_recipients_; }
    RuntimeDestination shown() const { return shown_; }

  private:
    static void on_product_timer(lv_timer_t *timer);
    static void requestNavigation(void *context, LoginDestination destination, const char *user);
    static void requestInbox(void *context, InboxDestination destination);
    static void requestTarget(void *context, MessageTargetRequest request);
    bool enqueue(const RuntimeNavigation &intent, bool replacePending = false);
    RuntimeNavigation intent(RuntimeDestination destination, bool animated, CoverDirection direction) const;
    void reconcileSession();
    bool install(const RuntimeNavigation &intent);
    void pollInput(int64_t now);
    void serviceFeatures();
    void present();
    RootViewController &root_;
    LoginService &service_;
    RuntimePlatform platform_;
    InboxService inbox_;
    SettingsService settings_;
    MessageTargetService targets_;
    lv_timer_t *timer_ = nullptr;
    bool muted_ = false;
    bool input_known_ = false;
    bool navigation_pending_ = false;
    RuntimeNavigation pending_;
    MessageRecipients recording_recipients_;
    RuntimeDestination shown_ = RuntimeDestination::Login;
    int64_t now_ = 0;
    int64_t navigation_retry_due_ = 0;
};

} // namespace product_ui
