#include "product_runtime.hpp"
#include <cassert>
#include <cstring>
#include <memory>

namespace
{
using namespace product_ui;
using namespace fl_product_login;
SessionWriter *writer;
int64_t now;
int muteLevel;
bool bootPressed;
bool redPressed;
int submittedSaves;
bool settingsPending;
SettingsResult settingsResult;

bool startLogin(SessionWriter *session)
{
    writer = session;
    return true;
}
void advanceLogin(SessionWriter *session, int64_t time)
{
    if (!session->roster_inflight())
        return;
    RosterResult result{};
    result.operation_id = session->roster_operation_id();
    result.http_status = 200;
    result.valid = result.parse_ok = 1;
    result.user_count = 3;
    const char *ids[] = {"u0", "u1", "u2"};
    for (int i = 0; i < 3; ++i) {
        std::strcpy(result.users[i].id, ids[i]);
        std::strcpy(result.users[i].display_name, ids[i]);
    }
    session->deliver_roster_completion(result, time);
}
void muteLogin() {}
int muteLatched()
{
    return muteLevel;
}
bool takeBoot()
{
    const bool p = bootPressed;
    bootPressed = false;
    return p;
}
bool takeRed()
{
    const bool p = redPressed;
    redPressed = false;
    return p;
}
bool startInbox()
{
    return true;
}
bool submitInbox(const product_inbox_http::Job *)
{
    return true;
}
const product_inbox_http::Result *pollInbox()
{
    return nullptr;
}
bool startSettings()
{
    return true;
}
bool submitSettings(const SettingsJob &job)
{
    if (job.kind == SettingsJobKind::Save)
        ++submittedSaves;
    settingsResult = {job.operation, job.kind, job.stop, true};
    settingsPending = true;
    return true;
}
bool pollSettings(SettingsResult &result)
{
    if (!settingsPending)
        return false;
    settingsPending = false;
    result = settingsResult;
    return true;
}

void run(ProductRuntime &runtime, RootViewController &root, int ticks = 1)
{
    for (int i = 0; i < ticks; ++i) {
        now += 50;
        runtime.tick(now);
        root.finishTransition();
    }
}
void wait(ProductRuntime &runtime, RootViewController &root, int64_t milliseconds)
{
    for (int64_t end = now + milliseconds; now < end;) {
        now += 250;
        runtime.tick(now);
        root.finishTransition();
    }
}
const char *current(RootViewController &root)
{
    return root.current() ? root.current()->name() : "None";
}
void openPin(ProductRuntime &runtime, RootViewController &root)
{
    auto *login = static_cast<LoginViewController *>(root.current());
    lv_obj_update_layout(login->view());
    assert(login->selectUser(0, "u0"));
    run(runtime, root);
    assert(runtime.shown() == RuntimeDestination::PinEntry && std::strcmp(current(root), "PinEntry") == 0);
}
PinResult submitPin(LoginService &service, int outcome)
{
    for (int i = 0; i < 4; ++i)
        assert(service.enterPinDigit('1' + i));
    PinResult result{};
    result.operation_id = writer->pin_operation_id();
    result.generation = writer->pin_key_generation();
    std::strcpy(result.session_id, writer->session_id());
    std::strcpy(result.user_id, "u0");
    result.http_status = outcome == kPinOutcomeSuccess ? 200 : 401;
    result.parse_ok = 1;
    result.outcome = outcome;
    if (outcome == kPinOutcomeSuccess) {
        std::strcpy(result.session_token, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        result.session_token_valid = 1;
    }
    return result;
}
void signIn(LoginService &service, ProductRuntime &runtime, RootViewController &root)
{
    openPin(runtime, root);
    writer->deliver_pin_completion(submitPin(service, kPinOutcomeSuccess), now);
    run(runtime, root);
    assert(runtime.shown() == RuntimeDestination::Inbox && std::strcmp(current(root), "Inbox") == 0);
}
void mute(ProductRuntime &runtime, RootViewController &root, bool latched)
{
    muteLevel = latched ? 1 : 0;
    run(runtime, root);
}
// The signed-in credential refreshes on its own schedule regardless of which
// signed-in controller is loaded; navigation and session identity are kept.
void refreshWhileShown(LoginService &service, ProductRuntime &runtime, RootViewController &root,
                       RuntimeDestination shown)
{
    const auto generation = service.session().generation();
    const auto inboxGeneration = runtime.inboxService().generation();
    const auto *view = root.current();
    int64_t waited = 0;
    while (!service.session().refresh_inflight()) {
        wait(runtime, root, 1000);
        waited += 1000;
        assert(waited <= kSessionRefreshPeriodMs + 5000);
    }
    RefreshResult refresh{};
    refresh.operation_id = service.session().refresh_operation_id();
    refresh.perform_ok = refresh.parse_ok = 1;
    refresh.http_status = 200;
    writer->deliver_refresh_completion(refresh, now);
    run(runtime, root, 2);
    assert(!service.session().refresh_inflight() && service.session().credential_active());
    assert(runtime.shown() == shown && root.current() == view);
    assert(service.session().generation() == generation && runtime.inboxService().generation() == inboxGeneration);
}
} // namespace

void testRuntimeRoutes(lv_obj_t *content)
{
    now = 1000;
    LoginService service({startLogin, advanceLogin, muteLogin});
    service.boot(now, true);
    RootViewController root(content);
    RuntimePlatform platform;
    platform.muteLatched = muteLatched;
    platform.takeBoot = takeBoot;
    platform.takeRed = takeRed;
    RuntimeServices services;
    services.inbox = {startInbox, submitInbox, pollInbox};
    services.settings = {startSettings, submitSettings, pollSettings};
    ProductRuntime runtime(root, service, platform, services);
    assert(root.replace(std::unique_ptr<ViewController>(new LoginViewController(service, runtime.navigation())), false));
    run(runtime, root, 3);
    assert(service.snapshot().body == LoginSnapshot::Body::Roster);

    // BOOT with no digits returns to the roster.
    auto *login = static_cast<LoginViewController *>(root.current());
    lv_obj_update_layout(login->view());
    assert(login->selectUser(0, "u0"));
    assert(!login->selectUser(1, "u1"));
    run(runtime, root);
    assert(std::strcmp(current(root), "PinEntry") == 0);
    bootPressed = true;
    now += 50;
    runtime.tick(now);
    assert(root.busy() && std::strcmp(current(root), "Login") == 0);
    auto *image = root.snapshotImage();
    assert(image && lv_obj_get_index(image) == static_cast<int32_t>(lv_obj_get_child_count(content)) - 1);
    assert(lv_obj_get_y(root.current()->view()) == 0);
    root.finishTransition();
    assert(!root.snapshotImage() && runtime.shown() == RuntimeDestination::Login);
    run(runtime, root);

    signIn(service, runtime, root);
    assert(runtime.inboxService().snapshot().body != InboxSnapshot::Body::NoWifi);

    // Inbox BOOT → Settings; BOOT saves through the service before returning.
    bootPressed = true;
    run(runtime, root, 2);
    assert(std::strcmp(current(root), "Settings") == 0);
    bootPressed = true;
    run(runtime, root, 3);
    assert(submittedSaves == 1 && std::strcmp(current(root), "Inbox") == 0);

    // RED → targets; a press made during the transition is consumed, not replayed.
    redPressed = true;
    now += 50;
    runtime.tick(now);
    assert(root.busy());
    bootPressed = true;
    now += 50;
    runtime.tick(now);
    root.finishTransition();
    run(runtime, root);
    assert(std::strcmp(current(root), "MessageTarget") == 0);
    const auto &targets = runtime.targetService().snapshot();
    assert(targets.count == 2 && targets.selected.count == 0);
    for (int i = 0; i < targets.count; ++i)
        assert(std::strcmp(targets.users[i].id, "u0") != 0);

    // BOOT returns to the same Inbox without creating a message.
    bootPressed = true;
    run(runtime, root, 2);
    assert(std::strcmp(current(root), "Inbox") == 0 && runtime.recordingRecipients().count == 0);

    redPressed = true;
    run(runtime, root, 2);
    assert(std::strcmp(current(root), "MessageTarget") == 0);
    redPressed = true;
    run(runtime, root, 2);
    assert(std::strcmp(current(root), "MessageTarget") == 0); // No valid recipient.
    auto *picker = static_cast<MessageTargetViewController *>(root.current());
    assert(picker->selectRecipient(1, targets.users[1].id));
    char chosen[kMaxId];
    std::strcpy(chosen, targets.users[1].id);
    redPressed = true;
    run(runtime, root, 2);
    assert(runtime.shown() == RuntimeDestination::Recording && std::strcmp(current(root), "Sketch") == 0);
    assert(runtime.recordingRecipients().count == 1);
    assert(std::strcmp(runtime.recordingRecipients().ids[0], chosen) == 0);

    // Mute signs out, releases the tree, and unmute returns to Login.
    muteLevel = 1;
    run(runtime, root);
    assert(runtime.muted() && !root.current() && !service.session().signed_in());
    assert(runtime.recordingRecipients().count == 0 && runtime.targetService().snapshot().count == 0);
    run(runtime, root, 4);
    assert(!root.current());
    muteLevel = 0;
    run(runtime, root, 3);
    assert(!runtime.muted() && runtime.shown() == RuntimeDestination::Login && std::strcmp(current(root), "Login") == 0);

    // Mute while Settings is shown saves and does not navigate on completion.
    signIn(service, runtime, root);
    bootPressed = true;
    run(runtime, root, 2);
    assert(std::strcmp(current(root), "Settings") == 0);
    const int saves = submittedSaves;
    muteLevel = 1;
    run(runtime, root, 3);
    assert(submittedSaves == saves + 1 && !root.current());
    muteLevel = 0;
    run(runtime, root, 3);
    assert(std::strcmp(current(root), "Login") == 0);

    // Repeated journeys keep a single live controller tree.
    const uint32_t children = lv_obj_get_child_count(content);
    for (int i = 0; i < 5; ++i) {
        signIn(service, runtime, root);
        bootPressed = true;
        run(runtime, root, 2);
        bootPressed = true;
        run(runtime, root, 3);
        redPressed = true;
        run(runtime, root, 2);
        muteLevel = 1;
        run(runtime, root);
        muteLevel = 0;
        run(runtime, root, 3);
        assert(lv_obj_get_child_count(content) == children);
    }

    // Credential refresh while Settings, targets, and the empty destination are shown.
    signIn(service, runtime, root);
    bootPressed = true;
    run(runtime, root);
    refreshWhileShown(service, runtime, root, RuntimeDestination::Settings);
    bootPressed = true;
    run(runtime, root, 2);
    redPressed = true;
    run(runtime, root);
    refreshWhileShown(service, runtime, root, RuntimeDestination::MessageTarget);
    picker = static_cast<MessageTargetViewController *>(root.current());
    assert(picker->selectRecipient(0, runtime.targetService().snapshot().users[0].id));
    redPressed = true;
    run(runtime, root);
    refreshWhileShown(service, runtime, root, RuntimeDestination::Recording);
    assert(runtime.recordingRecipients().count == 1);

    // BOOT and RED in one tick: the first intent wins; the second is rejected.
    bootPressed = true;
    run(runtime, root);
    assert(std::strcmp(current(root), "Inbox") == 0);
    mute(runtime, root, true);
    mute(runtime, root, false);
    signIn(service, runtime, root);
    bootPressed = redPressed = true;
    run(runtime, root, 2);
    assert(runtime.shown() == RuntimeDestination::Settings && std::strcmp(current(root), "Settings") == 0);

    // Mute mid-transition finishes the animation, releases the snapshot and tree.
    bootPressed = true;
    run(runtime, root, 3);
    assert(std::strcmp(current(root), "Inbox") == 0);
    redPressed = true;
    now += 50;
    runtime.tick(now);
    assert(root.busy() && root.snapshotImage());
    muteLevel = 1;
    now += 50;
    runtime.tick(now);
    assert(runtime.muted() && !root.busy() && !root.current() && !root.snapshotImage());
    assert(lv_obj_get_child_count(content) == children - 1);
    mute(runtime, root, false);
    assert(std::strcmp(current(root), "Login") == 0);

    // A PIN success that lands after mute neither signs in nor navigates.
    run(runtime, root);
    openPin(runtime, root);
    const auto late = submitPin(service, kPinOutcomeSuccess);
    mute(runtime, root, true);
    writer->deliver_pin_completion(late, now);
    run(runtime, root, 3);
    assert(!service.session().signed_in() && !root.current());
    mute(runtime, root, false);
    run(runtime, root, 2);
    assert(!service.session().signed_in() && runtime.shown() == RuntimeDestination::Login);
    assert(std::strcmp(current(root), "Login") == 0);

    // Cooldown survives mute with its original deadline, then returns to Login.
    openPin(runtime, root);
    for (int i = 0; i < kPinWrongLimit; ++i) {
        writer->deliver_pin_completion(submitPin(service, kPinOutcomeWrong), now);
        run(runtime, root);
        if (i < kPinWrongLimit - 1)
            wait(runtime, root, kWrongPinCopyMs);
    }
    assert(runtime.shown() == RuntimeDestination::Cooldown && std::strcmp(current(root), "PinCooldown") == 0);
    const int before = service.pinSnapshot().seconds;
    mute(runtime, root, true);
    assert(!root.current());
    wait(runtime, root, 10000);
    mute(runtime, root, false);
    assert(runtime.shown() == RuntimeDestination::Cooldown && std::strcmp(current(root), "PinCooldown") == 0);
    assert(service.pinSnapshot().seconds <= before - 9);
    wait(runtime, root, 60000);
    assert(runtime.shown() == RuntimeDestination::Login && std::strcmp(current(root), "Login") == 0);
}
