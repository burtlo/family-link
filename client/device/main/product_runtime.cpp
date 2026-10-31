#include "product_runtime.hpp"

#include "product_pin_entry_view_controller.hpp"
#include "product_record.hpp"

#include <cstdio>
#include <cstring>
#ifdef ESP_PLATFORM
#include "product_mic.hpp"
#include "product_playback_worker.hpp"
#include "product_send_http_worker.hpp"
#include "bsp/esp-box-3.h"
#include "esp_heap_caps.h"
#include "esp_random.h"
#endif
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
bool submitPlaybackMedia(const product_inbox_http::Job *job, void *context)
{
    return static_cast<ProductRuntime *>(context)->inboxService().submitExternal(job);
}

void forwardPlaybackMedia(void *context, const product_inbox_http::Result &result)
{
    static_cast<PlaybackService *>(context)->onMediaResult(result);
}

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
    if (input)
        lv_indev_reset(input, nullptr);
    ESP_LOGI("product_mute", "V2_MUTE_DISPLAY wake dark=0");
}
#endif

constexpr int64_t kNavigationRetryMs = 250;

const char *playbackRoot()
{
#ifdef ESP_PLATFORM
    return BSP_SD_MOUNT_POINT "/flplay";
#else
    return "playback-cache";
#endif
}

#ifdef ESP_PLATFORM
DeliveryHttpResult *delivery_http_slot = nullptr;

void *psramAlloc(size_t bytes, void *)
{
    return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
}

void psramFree(void *block, void *)
{
    heap_caps_free(block);
}

void *allocateSurface(size_t bytes, SurfacePlace requested, SurfacePlace *placed, void *)
{
    const uint32_t caps = requested == SurfacePlace::Internal ? (MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT)
                                                              : (MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
    void *block = heap_caps_malloc(bytes, caps);
    if (block && placed)
        *placed = requested == SurfacePlace::Internal ? SurfacePlace::Internal : SurfacePlace::Psram;
    return block;
}
void releaseSurface(void *block, void *)
{
    heap_caps_free(block);
}
#endif

bool signedInDestination(RuntimeDestination destination)
{
    return destination == RuntimeDestination::Inbox || destination == RuntimeDestination::Settings ||
           destination == RuntimeDestination::MessageTarget || destination == RuntimeDestination::Recording ||
           destination == RuntimeDestination::SendFeedback;
}
} // namespace

ProductRuntime::ProductRuntime(RootViewController &root, LoginService &service, RuntimePlatform platform,
                               RuntimeServices services)
    : root_(root), service_(service), platform_(platform), media_(playbackRoot()), playback_(media_, surfaces_),
      inbox_(services.inbox), settings_(services.settings), delivery_(capture_)
{
    const char *mediaPath = services.mediaPath;
#ifdef ESP_PLATFORM
    mediaPath = BSP_SD_MOUNT_POINT "/flmsgs.bin";
    capture_.setAllocator(psramAlloc, psramFree, nullptr);
    capture_.setHardware(product_mic::set_armed, product_mic::take_failure);
    product_record::set_pcm_observer(
        [](const uint8_t *pcm, size_t length, void *context) {
            return static_cast<CaptureService *>(context)->pushPcm(pcm, length) ? 1 : 0;
        },
        &capture_);
#else
    if (!mediaPath)
        mediaPath = "record-media.bin";
    if (services.captureAlloc && services.captureFree)
        capture_.setAllocator(services.captureAlloc, services.captureFree, services.captureAllocContext);
    delivery_.noteSeed(1);
#endif
    std::snprintf(media_path_, sizeof(media_path_), "%s", mediaPath ? mediaPath : "");
    DeliveryTransport transport = services.delivery;
#ifdef ESP_PLATFORM
    if (!transport.start) {
        transport.start = product_send_http::worker_start;
        transport.submit = product_send_http::submit;
        delivery_http_slot = &delivery_.polledSlot();
        transport.poll = []() -> const DeliveryHttpResult * {
            const auto *done = product_send_http::poll();
            if (!done || !delivery_http_slot)
                return nullptr;
            DeliveryHttpResult &copy = *delivery_http_slot;
            copy = {};
            copy.perform_ok = done->perform_ok;
            copy.http_status = done->http_status;
            copy.body_len = done->body_len;
            copy.seq = done->seq;
            std::snprintf(copy.client_message_id, sizeof(copy.client_message_id), "%s", done->client_message_id);
            const int n = done->body_len < static_cast<int>(sizeof(copy.body) - 1) ? done->body_len
                                                                                   : static_cast<int>(sizeof(copy.body) - 1);
            if (n > 0)
                std::memcpy(copy.body, done->body, static_cast<size_t>(n));
            copy.body[n > 0 ? n : 0] = 0;
            copy.body_len = n > 0 ? n : 0;
            return &copy;
        };
    }
#endif
    delivery_.setTransport(transport);
    delivery_.setPlayback(pauseForDelivery, selectedForDelivery, this);
    playback_.setMediaSubmit(submitPlaybackMedia, this);
    inbox_.setMediaSink(forwardPlaybackMedia, &playback_);
    if (services.playback.start || services.playback.submit || services.playback.begin_session)
        playback_.setTransport(services.playback);
    if (services.audio.start || services.audio.set_volume || services.audio.accept)
        playback_.setAudio(services.audio);
#ifdef ESP_PLATFORM
    surfaces_.setAllocator(allocateSurface, releaseSurface, nullptr);
    playback_.setTransport(playback_worker::transport());
    playback_.setAudio(playback_worker::audio());
    playback_.setUnready([](void *context) { static_cast<ProductRuntime *>(context)->inboxService().noteSessionUnready(); },
                         this);
#endif
#ifdef ESP_PLATFORM
    if (!platform_.muteLatched)
        platform_.muteLatched = product_buttons::mute_latched;
    if (!platform_.blankDisplay)
        platform_.blankDisplay = blank_login_display;
    if (!platform_.wakeDisplay)
        platform_.wakeDisplay = wake_login_display;
    if (!platform_.takeBoot)
        platform_.takeBoot = product_buttons::take_boot_press;
    if (!platform_.takeRed)
        platform_.takeRed = product_buttons::take_red_press;
#endif
}

ProductRuntime::~ProductRuntime()
{
#ifdef ESP_PLATFORM
    product_record::set_pcm_observer(nullptr, nullptr);
#endif
    if (timer_)
        lv_timer_delete(timer_);
}

bool ProductRuntime::start()
{
    if (timer_)
        return true;
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
    if (muted_ || (navigation_pending_ && !replacePending))
        return false;
    if (next.destination == shown_) {
        navigation_pending_ = false;
        return false;
    }
    if (next.destination == RuntimeDestination::Inbox &&
        (shown_ == RuntimeDestination::Recording || shown_ == RuntimeDestination::SendFeedback ||
         shown_ == RuntimeDestination::MessageTarget))
        restore_inbox_ = true;
    pending_ = next;
    navigation_pending_ = true;
    navigation_retry_due_ = 0;
    return true;
}

void ProductRuntime::requestNavigation(void *context, LoginDestination destination, const char *user)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (!user || !*user || destination != LoginDestination::PinEntry || runtime->root_.busy() || runtime->muted_ ||
        runtime->navigation_pending_ || runtime->shown_ != RuntimeDestination::Login)
        return;
    if (!runtime->service_.beginPin(user))
        return;
    auto next = runtime->intent(RuntimeDestination::PinEntry, true, CoverDirection::Forward);
    std::strncpy(next.user, user, sizeof(next.user) - 1);
    (void)runtime->enqueue(next);
}

void ProductRuntime::requestInbox(void *context, InboxDestination destination)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (runtime->root_.busy() || runtime->shown_ != RuntimeDestination::Inbox ||
        !runtime->service_.session().signed_in())
        return;
    if (destination == InboxDestination::Settings) {
        (void)runtime->enqueue(runtime->intent(RuntimeDestination::Settings, true, CoverDirection::Forward));
        return;
    }
    const auto result = runtime->delivery_.apply(fl_product_send::kSendIntentRed, runtime->now_, true);
    if (result.ignored)
        return;
    if (runtime->delivery_.showsFeedback())
        (void)runtime->enqueue(runtime->intent(RuntimeDestination::SendFeedback, true, CoverDirection::Forward));
    else if (runtime->enqueue(runtime->intent(RuntimeDestination::MessageTarget, true, CoverDirection::Forward)))
        runtime->targets_.beginJourney();
}

void ProductRuntime::requestTarget(void *context, MessageTargetRequest request)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (runtime->root_.busy() || runtime->shown_ != RuntimeDestination::MessageTarget ||
        !runtime->service_.session().signed_in())
        return;
    if (request.destination == MessageTargetDestination::Inbox) {
        (void)runtime->delivery_.apply(fl_product_send::kSendIntentBoot, runtime->now_, true);
        (void)runtime->enqueue(runtime->intent(RuntimeDestination::Inbox, true, CoverDirection::Reverse));
        return;
    }
    if (request.recipients.count <= 0)
        return;
    runtime->delivery_.mirrorSelection(runtime->targets_.snapshot());
    const auto result = runtime->delivery_.apply(fl_product_send::kSendIntentRed, runtime->now_, true);
    if (result.ignored || !runtime->delivery_.showsSketch())
        return;
    auto next = runtime->intent(RuntimeDestination::Recording, true, CoverDirection::Forward);
    next.recipients = request.recipients;
    (void)runtime->enqueue(next);
}

void ProductRuntime::requestSketch(void *context, SketchIntent intent)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (runtime->root_.busy() || runtime->shown_ != RuntimeDestination::Recording || !runtime->service_.session().signed_in())
        return;
    if (intent == SketchIntent::Boot) {
        (void)runtime->delivery_.apply(fl_product_send::kSendIntentBoot, runtime->now_, true);
        (void)runtime->enqueue(runtime->intent(RuntimeDestination::Inbox, true, CoverDirection::Reverse));
        return;
    }
    (void)runtime->delivery_.apply(fl_product_send::kSendIntentRed, runtime->now_, true);
}

void ProductRuntime::requestFeedback(void *context)
{
    auto *runtime = static_cast<ProductRuntime *>(context);
    if (runtime->root_.busy() || runtime->shown_ != RuntimeDestination::SendFeedback)
        return;
    (void)runtime->delivery_.apply(fl_product_send::kSendIntentBoot, runtime->now_, true);
    (void)runtime->enqueue(runtime->intent(RuntimeDestination::Inbox, true, CoverDirection::Reverse));
}

void ProductRuntime::pauseForDelivery(void *context)
{
    static_cast<ProductRuntime *>(context)->playback_.pausePreserving();
}

const char *ProductRuntime::selectedForDelivery(void *context)
{
    const char *id = static_cast<ProductRuntime *>(context)->inbox_.selectedMessageId();
    return id ? id : "";
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
            if (platform_.blankDisplay)
                platform_.blankDisplay();
            if (shown_ == RuntimeDestination::Settings)
                (void)settings_.prepareLeave(SettingsLeaveReason::Mute);
            (void)delivery_.apply(fl_product_send::kSendIntentMute, now, service_.session().signed_in());
            service_.signOut(now);
            playback_.signOut();
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
            if (platform_.wakeDisplay)
                platform_.wakeDisplay();
        }
    }
    // Presses are always consumed so one made where no handler exists cannot
    // fire later on a controller that does handle it.
    const bool boot = platform_.takeBoot && platform_.takeBoot();
    const bool red = platform_.takeRed && platform_.takeRed();
    auto *controller = root_.current();
    if (!muted_ && !root_.busy() && !navigation_pending_ && controller) {
        if (boot && controller->handlesBoot())
            root_.dispatchBoot();
        if (red && controller->handlesRed())
            root_.dispatchRed();
    }
}

void ProductRuntime::serviceFeatures()
{
    const auto &session = service_.session();
    const bool signedIn = !muted_ && session.signed_in();
    const bool credentialReady = signedIn && session.credential_active() && !session.credential_expired();
    inbox_.setSession(signedIn ? session.signed_user() : nullptr, signedIn ? session.credential_token() : "",
                      session.generation(), service_.wifiUp(), credentialReady, now_);
    inbox_.setFetchHeld(capture_.blocksInboxFetch());
    inbox_.advance(now_);
    playback_.drainMaintenance();
    if (signedIn) {
        if (!was_signed_in_ && media_path_[0])
            (void)capture_.open(media_path_);
        const bool playback_allowed = credentialReady && service_.wifiUp() &&
                                      inbox_.snapshot().body == InboxSnapshot::Body::Cards;
        playback_.setSession(session.signed_user(), session.generation(), session.credential_token(), playback_allowed,
                             settings_.codecVolume(), now_);
        playback_.advance(now_);
        const bool refreshFailed = session.credential_expired();
        delivery_.setSession(session.signed_user(), session.credential_token(), service_.wifiUp(), refreshFailed,
                             session.generation(), now_);
        delivery_.advance(now_);
    } else if (was_signed_in_) {
        playback_.signOut();
        delivery_.setSession(nullptr, "", service_.wifiUp(), false, session.generation(), now_);
    }
    was_signed_in_ = signedIn;
#ifdef ESP_PLATFORM
    if (service_.wifiUp() && !delivery_seeded_) {
        delivery_.noteSeed(esp_random());
        delivery_seeded_ = true;
    }
#endif
    settings_.advance(now_);
    followDelivery();
    restoreInboxSelection();
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

void ProductRuntime::followDelivery()
{
    if (muted_ || root_.busy() || navigation_pending_ || !service_.session().signed_in())
        return;
    targets_.setNotice(delivery_.couldntRecord() ? "couldn't record" : "");
    if (shown_ == RuntimeDestination::Recording && !delivery_.showsSketch()) {
        const auto destination = delivery_.showsFeedback() ? RuntimeDestination::SendFeedback
                                 : delivery_.couldntRecord() || delivery_.place() == fl_product_send::kSendPlacePicker
                                     ? RuntimeDestination::MessageTarget
                                     : RuntimeDestination::Inbox;
        (void)enqueue(intent(destination, true, CoverDirection::Reverse));
    } else if (shown_ == RuntimeDestination::SendFeedback && !delivery_.showsFeedback()) {
        (void)enqueue(intent(RuntimeDestination::Inbox, true, CoverDirection::Reverse));
    }
}

void ProductRuntime::restoreInboxSelection()
{
    if (!restore_inbox_ || shown_ != RuntimeDestination::Inbox)
        return;
    const auto body = inbox_.snapshot().body;
    if (body != InboxSnapshot::Body::Cards && body != InboxSnapshot::Body::Empty)
        return;
    const char *id = delivery_.rememberedSelection();
    if (id && id[0])
        (void)inbox_.retainSelection(id);
    restore_inbox_ = false;
}

void ProductRuntime::reconcileSession()
{
    if (muted_)
        return;
    const auto place = service_.pinSnapshot().place;
    const bool signedIn = service_.session().signed_in();
    const RuntimeDestination target = navigation_pending_ ? pending_.destination : shown_;
    RuntimeDestination desired = target;
    CoverDirection direction = CoverDirection::Forward;
    if (signedInDestination(target)) {
        if (!signedIn) {
            desired = RuntimeDestination::Login;
            direction = CoverDirection::Reverse;
        }
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
    if (desired == target)
        return;
    // Authentication state supersedes any queued controller intent.
    (void)enqueue(intent(desired, true, direction), true);
}

bool ProductRuntime::install(const RuntimeNavigation &next)
{
    std::unique_ptr<ViewController> controller;
    switch (next.destination) {
        case RuntimeDestination::Login:
            controller.reset(new LoginViewController(service_, navigation()));
            break;
        case RuntimeDestination::PinEntry:
            controller.reset(new PinEntryViewController(service_));
            break;
        case RuntimeDestination::Cooldown:
            controller.reset(new PinCooldownViewController(service_));
            break;
        case RuntimeDestination::Inbox:
            controller.reset(new InboxViewController(inbox_, playback_, {this, requestInbox}));
            break;
        case RuntimeDestination::Settings:
            controller.reset(new SettingsViewController(settings_));
            break;
        case RuntimeDestination::MessageTarget:
            controller.reset(new MessageTargetViewController(targets_, {this, requestTarget}));
            break;
        case RuntimeDestination::Recording:
            controller.reset(new SketchViewController(delivery_, surfaces_, {this, requestSketch}));
            break;
        case RuntimeDestination::SendFeedback:
            controller.reset(new SendFeedbackViewController(delivery_, {this, requestFeedback}));
            break;
    }
    if (!controller || !root_.replace(std::move(controller), next.animated, next.direction)) {
        if (next.destination != RuntimeDestination::Recording)
            return false;
        (void)delivery_.apply(fl_product_send::kSendIntentBoot, now_, true);
        controller.reset(new InboxViewController(inbox_, playback_, {this, requestInbox}));
        if (!controller || !root_.replace(std::move(controller), false, CoverDirection::Reverse))
            return false;
        shown_ = RuntimeDestination::Inbox;
        restore_inbox_ = true;
        recording_recipients_ = {};
        return true;
    }
    shown_ = next.destination;
    if (next.destination == RuntimeDestination::Recording)
        recording_recipients_ = next.recipients;
    else
        recording_recipients_ = {};
    return true;
}

void ProductRuntime::present()
{
    if (muted_)
        return;
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
            if (install(immediate))
                navigation_pending_ = false;
            else
                navigation_retry_due_ = now_ + kNavigationRetryMs;
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
