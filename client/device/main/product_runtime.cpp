#include "product_runtime.hpp"

#include "product_pin_entry_view_controller.hpp"
#include <cstring>
#ifdef ESP_PLATFORM
#include "product_buttons.hpp"
#include "bsp/esp-box-3.h"
#include "esp_log.h"
#include "esp_timer.h"
#endif

namespace product_ui
{

namespace
{
#ifdef ESP_PLATFORM
void blank_login_display()
{
    (void)bsp_display_backlight_off();
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY blank dark=1");
}

void wake_login_display()
{
    (void)bsp_display_brightness_set(80);
    lv_indev_t *input = bsp_display_get_input_dev();
    if (input) lv_indev_reset(input, nullptr);
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY wake dark=0");
}
#endif

constexpr int64_t kNavigationRetryMs = 250;

bool signedInDestination(RuntimeDestination destination)
{
    return destination == RuntimeDestination::Inbox || destination == RuntimeDestination::Settings ||
           destination == RuntimeDestination::MessageTarget || destination == RuntimeDestination::Recording;
}
} // namespace

ProductRuntime::ProductRuntime(RootViewController &root, LoginService &service, RuntimePlatform platform,
                               RuntimeServices services)
    : root_(root), service_(service), platform_(platform), inbox_(services.inbox), settings_(services.settings)
{
#ifdef ESP_PLATFORM
    if (!platform_.muteLatched) platform_.muteLatched = product_buttons::mute_latched;
    if (!platform_.blankDisplay) platform_.blankDisplay = blank_login_display;
    if (!platform_.wakeDisplay) platform_.wakeDisplay = wake_login_display;
    if (!platform_.takeBoot) platform_.takeBoot = product_buttons::take_boot_press;
    if (!platform_.takeRed) platform_.takeRed = product_buttons::take_red_press;
#endif
}

ProductRuntime::~ProductRuntime()
{
    if (timer_) lv_timer_delete(timer_);
}

bool ProductRuntime::start()
{
    if (timer_) return true;
    timer_ = lv_timer_create(on_product_timer, 50, this);
    return timer_ != nullptr;
}

void ProductRuntime::on_product_timer(lv_timer_t *timer)
{
    auto *runtime = static_cast<ProductRuntime *>(lv_timer_get_user_data(timer));
#ifdef ESP_PLATFORM
    runtime->tick(esp_timer_get_time() / 1000LL);
#else
    runtime->tick(lv_tick_get());
#endif
}

LoginNavigation ProductRuntime::navigation()
{
    return {this, requestNavigation};
}

RuntimeNavigation ProductRuntime::intent(RuntimeDestination destination, bool animated,
                                         CoverDirection direction) const
{
    RuntimeNavigation result;
    result.destination = destination;
    result.generation = service_.session().generation();
    result.animated = animated;
    result.direction = direction;
    return result;
}

bool ProductRuntime::enqueue(const RuntimeNavigation &next, bool replacePending)
{
    if (muted_ || (navigation_pending_ && !replacePending)) return false;
    if (next.destination == shown_) {
        navigation_pending_ = false;
        return false;
    }
    pending_ = next;
    navigation_pending_ = true;
    navigation_retry_due_ = 0;
    return true;
}

void ProductRuntime::requestNavigation(void *context, LoginDestination destination, const char *user)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (!user || !*user || destination != LoginDestination::PinEntry || runtime->root_.busy() || runtime->muted_ ||
        runtime->navigation_pending_ || runtime->shown_ != RuntimeDestination::Login) return;
    if (!runtime->service_.beginPin(user)) return;
    auto next = runtime->intent(RuntimeDestination::PinEntry, true, CoverDirection::Forward);
    std::strncpy(next.user, user, sizeof(next.user) - 1);
    (void)runtime->enqueue(next);
}

void ProductRuntime::requestInbox(void *context, InboxDestination destination)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (runtime->root_.busy() || runtime->shown_ != RuntimeDestination::Inbox ||
        !runtime->service_.session().signed_in()) return;
    if (destination == InboxDestination::Settings) {
        (void)runtime->enqueue(runtime->intent(RuntimeDestination::Settings, true, CoverDirection::Forward));
    } else if (runtime->enqueue(runtime->intent(RuntimeDestination::MessageTarget, true, CoverDirection::Forward))) {
        runtime->targets_.beginJourney();
    }
}

void ProductRuntime::requestTarget(void *context, MessageTargetRequest request)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (runtime->root_.busy() || runtime->shown_ != RuntimeDestination::MessageTarget ||
        !runtime->service_.session().signed_in()) return;
    if (request.destination == MessageTargetDestination::Inbox) {
        (void)runtime->enqueue(runtime->intent(RuntimeDestination::Inbox, true, CoverDirection::Reverse));
        return;
    }
    if (request.recipients.count <= 0) return;
    auto next = runtime->intent(RuntimeDestination::Recording, true, CoverDirection::Forward);
    next.recipients = request.recipients;
    (void)runtime->enqueue(next);
}

void ProductRuntime::pollInput(int64_t now)
{
    const bool latched = platform_.muteLatched && platform_.muteLatched() != 0;
    if (!input_known_ || latched != muted_) {
        input_known_ = true;
#ifdef ESP_PLATFORM
        ESP_LOGI("product_mute", "V2_MUTE_EDGE latch=%d", latched ? 1 : 0);
#endif
        if (latched) {
            root_.finishTransition();
            if (platform_.blankDisplay) platform_.blankDisplay();
            if (shown_ == RuntimeDestination::Settings) (void)settings_.prepareLeave(SettingsLeaveReason::Mute);
            service_.signOut(now);
            inbox_.signOut();
            targets_.reset();
            recording_recipients_ = {};
            navigation_pending_ = false;
            root_.clear();
            muted_ = true;
        } else if (muted_) {
            service_.refreshHangout(now);
            muted_ = false;
            // An unexpired PIN cooldown keeps its original deadline across mute.
            const bool cooldown = service_.pinSnapshot().place == PinSnapshot::Place::Cooldown;
            const auto next = intent(cooldown ? RuntimeDestination::Cooldown : RuntimeDestination::Login, false,
                                     CoverDirection::Forward);
            if (!install(next)) {
                pending_ = next;
                navigation_pending_ = true;
                navigation_retry_due_ = now + kNavigationRetryMs;
            }
            root_.render();
            if (platform_.wakeDisplay) platform_.wakeDisplay();
        }
    }
    // Presses are always consumed so one made where no handler exists cannot
    // fire later on a controller that does handle it.
    const bool boot = platform_.takeBoot && platform_.takeBoot();
    const bool red = platform_.takeRed && platform_.takeRed();
    auto *controller = root_.current();
    if (!muted_ && !root_.busy() && !navigation_pending_ && controller) {
        if (boot && controller->handlesBoot()) root_.dispatchBoot();
        if (red && controller->handlesRed()) root_.dispatchRed();
    }
}

void ProductRuntime::serviceFeatures()
{
    const auto &session = service_.session();
    const bool signedIn = !muted_ && session.signed_in();
    const bool credentialReady = signedIn && session.credential_active() && !session.credential_expired();
    inbox_.setSession(signedIn ? session.signed_user() : nullptr, signedIn ? session.credential_token() : "",
                      session.generation(), service_.wifiUp(), credentialReady, now_);
    inbox_.advance(now_);
    settings_.advance(now_);
    if (signedIn) {
        const auto roster = service_.snapshot();
        targets_.setRoster(session.signed_user(), roster.users, roster.count);
    } else {
        targets_.reset();
    }
    SettingsLeaveCompletion completion;
    if (settings_.takeLeaveCompletion(completion) && completion.reason == SettingsLeaveReason::Boot &&
        shown_ == RuntimeDestination::Settings && signedIn) {
        (void)enqueue(intent(RuntimeDestination::Inbox, true, CoverDirection::Reverse));
    }
    reconcileSession();
}

void ProductRuntime::reconcileSession()
{
    if (muted_) return;
    const auto place = service_.pinSnapshot().place;
    const bool signedIn = service_.session().signed_in();
    const RuntimeDestination target = navigation_pending_ ? pending_.destination : shown_;
    RuntimeDestination desired = target;
    CoverDirection direction = CoverDirection::Forward;
    if (signedInDestination(target)) {
        if (!signedIn) { desired = RuntimeDestination::Login; direction = CoverDirection::Reverse; }
    } else if (place == PinSnapshot::Place::Cooldown) {
        desired = RuntimeDestination::Cooldown;
    } else if (place == PinSnapshot::Place::Inbox) {
        desired = RuntimeDestination::Inbox;
        direction = CoverDirection::Reverse;
    } else if (place == PinSnapshot::Place::Roster && target != RuntimeDestination::Login) {
        desired = RuntimeDestination::Login;
        direction = CoverDirection::Reverse;
    } else if (place == PinSnapshot::Place::PinEntry && target == RuntimeDestination::Cooldown) {
        desired = RuntimeDestination::PinEntry;
        direction = CoverDirection::Reverse;
    }
    if (desired == target) return;
    // Authentication state supersedes any queued controller intent.
    (void)enqueue(intent(desired, true, direction), true);
}

bool ProductRuntime::install(const RuntimeNavigation &next)
{
    std::unique_ptr<ViewController> controller;
    switch (next.destination) {
    case RuntimeDestination::Login: controller.reset(new LoginViewController(service_, navigation())); break;
    case RuntimeDestination::PinEntry: controller.reset(new PinEntryViewController(service_)); break;
    case RuntimeDestination::Cooldown: controller.reset(new PinCooldownViewController(service_)); break;
    case RuntimeDestination::Inbox: controller.reset(new InboxViewController(inbox_, {this, requestInbox})); break;
    case RuntimeDestination::Settings: controller.reset(new SettingsViewController(settings_)); break;
    case RuntimeDestination::MessageTarget:
        controller.reset(new MessageTargetViewController(targets_, {this, requestTarget}));
        break;
    case RuntimeDestination::Recording: controller.reset(new NoDestinationViewController); break;
    }
    if (!controller || !root_.replace(std::move(controller), next.animated, next.direction)) return false;
    shown_ = next.destination;
    if (next.destination == RuntimeDestination::Recording) recording_recipients_ = next.recipients;
    else recording_recipients_ = {};
    return true;
}

void ProductRuntime::present()
{
    if (muted_) return;
    if (navigation_pending_ && !root_.busy() && now_ >= navigation_retry_due_) {
        const auto &session = service_.session();
        if (signedInDestination(pending_.destination) &&
            (!session.signed_in() || pending_.generation != session.generation())) {
            navigation_pending_ = false;
        } else if (install(pending_)) {
            navigation_pending_ = false;
        } else {
            // A failed snapshot leaves the outgoing controller intact; the
            // destination is still owed, so fall back to an immediate swap.
            auto immediate = pending_;
            immediate.animated = false;
            if (install(immediate)) navigation_pending_ = false;
            else navigation_retry_due_ = now_ + kNavigationRetryMs;
        }
    }
    root_.render();
}

void ProductRuntime::tick(int64_t now)
{
    now_ = now;
    pollInput(now);
    service_.flushConnectivity(now);
    service_.advance(now);
    serviceFeatures();
    present();
}

} // namespace product_ui
