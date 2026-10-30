#include "product_pin_entry_view_controller.hpp"
#include <cassert>
#include <cstdio>
#include <cstring>
#include <memory>

void advancePinTestClock(uint32_t milliseconds);

namespace
{
using namespace product_ui;
using namespace fl_product_login;
SessionWriter *writer;
bool autoRoster;
int64_t now;
bool start(SessionWriter *session) { writer = session; return true; }
void advance(SessionWriter *session, int64_t time)
{
    if (autoRoster && session->roster_inflight()) {
        RosterResult result{};
        result.operation_id = session->roster_operation_id();
        result.http_status = 200;
        result.valid = result.parse_ok = 1;
        result.user_count = 1;
        std::strcpy(result.users[0].id, "u0");
        std::strcpy(result.users[0].display_name, "Person Long Name");
        session->deliver_roster_completion(result, time);
    }
}
void mute() {}
void step(LoginService &service, int milliseconds)
{
    for (int remaining = milliseconds; remaining > 0;) {
        int delta = remaining < 50 ? remaining : 50;
        now += delta;
        service.advance(now);
        remaining -= delta;
    }
}
void ready(LoginService &service)
{
    autoRoster = true;
    service.boot(now, true);
    service.advance(now);
    service.advance(now);
    assert(service.beginPin("u0"));
}
PinResult resultFor(int outcome)
{
    PinResult result{};
    result.operation_id = writer->pin_operation_id();
    result.generation = writer->pin_key_generation();
    std::strcpy(result.session_id, writer->session_id());
    std::strcpy(result.user_id, writer->focus_id());
    result.http_status = outcome == kPinOutcomeSuccess ? 200 : 401;
    result.parse_ok = 1;
    result.outcome = outcome;
    if (outcome == kPinOutcomeSuccess) {
        std::strcpy(result.session_token, "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA");
        result.session_token_valid = 1;
    }
    return result;
}
void complete(LoginService &service, int outcome)
{
    writer->deliver_pin_completion(resultFor(outcome), now);
    service.advance(now);
}
void submit(LoginService &service)
{
    for (int i = 0; i < 4; ++i) assert(service.enterPinDigit('1' + i));
    assert(service.pinSnapshot().checking);
}
}

void testPinControllers(lv_obj_t *content)
{
    using namespace product_ui;
    using namespace fl_product_login;
    now = 100;
    auto service = std::unique_ptr<LoginService>(new LoginService({start, advance, mute}));
    ready(*service);
    assert(!service->beginPin("missing"));
    assert(!service->enterPinDigit('0'));
    {
        RootViewController root(content);
        auto *pin = new PinEntryViewController(*service);
        assert(root.replace(std::unique_ptr<ViewController>(pin), false));
        lv_obj_update_layout(content);
        for (int i = 1; i <= 9; ++i) {
            auto *key = pin->keyAt(i);
            assert(key && lv_obj_get_width(key) == 101 && lv_obj_get_height(key) == 48);
            assert(lv_obj_get_x(key) == 4 + ((i - 1) % 3) * 105);
            assert(lv_obj_get_y(key) == 4 + ((i - 1) / 3) * 52);
        }
        assert(pin->keyAt(0) == nullptr);
        assert(std::strcmp(lv_label_get_text(pin->identityLabel()), "Person Long Name") == 0);
        lv_obj_send_event(pin->keyAt(1), LV_EVENT_PRESSED, nullptr);
        assert(service->pinSnapshot().digits == 1);
        root.dispatchBoot();
        assert(service->pinSnapshot().digits == 0);
        for (int i = 1; i <= 4; ++i) lv_obj_send_event(pin->keyAt(i), LV_EVENT_PRESSED, nullptr);
        assert(service->pinSnapshot().checking);
        assert(std::strcmp(lv_label_get_text(pin->marksLabel()), "checking...") == 0);
        assert(!lv_obj_is_clickable(pin->keyAt(1)));
        root.dispatchBoot();
        assert(service->pinSnapshot().checking);
        complete(*service, kPinOutcomeWrong);
        root.render();
        assert(service->pinSnapshot().wrong);
        assert(std::strcmp(lv_label_get_text(pin->identityLabel()), "wrong pin") == 0);
        assert(pin->enterDigit('2'));
        assert(!service->pinSnapshot().wrong && service->pinSnapshot().digits == 1);
        service->scheduleWifi(false);
        service->flushConnectivity(now);
        root.render();
        assert(service->pinSnapshot().connectionStatus);
        assert(std::strcmp(lv_label_get_text(pin->statusLabel()), "no Wi-Fi") == 0);
        advancePinTestClock(90);
        root.render();
        assert(!lv_obj_is_hidden(pin->statusLabel()));
        assert(lv_obj_is_hidden(lv_obj_get_parent(pin->keyAt(1))));
        advancePinTestClock(90);
        root.render();
        assert(lv_obj_get_style_opa(pin->statusLabel(), LV_PART_MAIN) == LV_OPA_COVER);
        root.dispatchBoot();
        assert(service->pinSnapshot().digits == 1);
        step(*service, 61000);
        assert(service->pinSnapshot().digits == 1); // Status pauses partial-entry timeout.
        service->scheduleWifi(true);
        service->flushConnectivity(now);
        service->advance(now);
        service->advance(now);
        assert(!service->pinSnapshot().connectionStatus);
        root.dispatchBoot();
        root.dispatchBoot();
        assert(service->pinSnapshot().place == PinSnapshot::Place::Roster);
        assert(!pin->enterDigit('1'));
    }
    assert(service->beginPin("u0"));
    {
        PinEntryViewController pin(*service);
        assert(pin.loadView(content));
        pin.unloadView();
        assert(pin.keyAt(1) == nullptr && pin.upperSlot() == nullptr && pin.statusLabel() == nullptr);
        assert(!pin.enterDigit('1'));
        assert(pin.loadView(content));
        pin.unloadView();
        const auto children = lv_obj_get_child_count(content);
        void *blocks[512]{};
        int count = 0;
        while (count < 512 && (blocks[count] = lv_malloc(128))) ++count;
        assert(!pin.loadView(content));
        assert(!pin.loaded() && !pin.view() && !pin.keyAt(1));
        for (int i = 0; i < count; ++i) lv_free(blocks[i]);
        assert(lv_obj_get_child_count(content) == children);
        assert(pin.loadView(content));
        pin.unloadView();
    }
    // Five failures advance to a service-owned full-screen cooldown.
    for (int i = 0; i < kPinWrongLimit; ++i) {
        submit(*service);
        complete(*service, kPinOutcomeWrong);
        if (i < kPinWrongLimit - 1) step(*service, kWrongPinCopyMs);
    }
    assert(service->pinSnapshot().place == PinSnapshot::Place::Cooldown);
    {
        RootViewController root(content);
        auto *cooldown = new PinCooldownViewController(*service);
        assert(root.replace(std::unique_ptr<ViewController>(cooldown), false));
        assert(lv_obj_get_child_count(cooldown->view()) == 2);
        assert(std::strcmp(lv_label_get_text(cooldown->countdownLabel()), "Try again in 60.") == 0);
        service->signOut(now);
        step(*service, 11000);
        assert(service->pinSnapshot().seconds == 49);
        service->refreshHangout(now);
        root.render();
        assert(service->pinSnapshot().place == PinSnapshot::Place::Cooldown);
        assert(std::strcmp(lv_label_get_text(cooldown->countdownLabel()), "Try again in 49.") == 0);
        service->scheduleWifi(false);
        service->flushConnectivity(now);
        step(*service, 49000);
        assert(service->pinSnapshot().place == PinSnapshot::Place::Roster);
    }
    service->scheduleWifi(true);
    service->flushConnectivity(now);
    service->advance(now);
    service->advance(now);
    assert(service->beginPin("u0"));
    service->enterPinDigit('1');
    step(*service, kPartialPinIdleMs);
    assert(service->pinSnapshot().place == PinSnapshot::Place::Roster);
    assert(service->beginPin("u0"));
    submit(*service);
    const auto late = resultFor(kPinOutcomeSuccess);
    service->signOut(now);
    writer->deliver_pin_completion(late, now);
    service->advance(now);
    assert(!service->session().signed_in());
    service->refreshHangout(now);
    service->advance(now);
    service->advance(now);
    assert(service->beginPin("u0"));
    submit(*service);
    complete(*service, kPinOutcomeSuccess);
    assert(service->pinSnapshot().place == PinSnapshot::Place::Inbox);
    assert(std::strcmp(service->session().signed_user(), "u0") == 0);
    // Rendering/reloading controllers does not change the signed-in state.
    assert(!service->beginPin("u0"));
    service->signOut(now);
    service->refreshHangout(now);
    service->advance(now);
    service->advance(now);
    assert(service->beginPin("u0"));
    submit(*service);
    complete(*service, kPinOutcomeUnknownUser);
    assert(service->pinSnapshot().place == PinSnapshot::Place::Roster);
    service->advance(now);
    assert(service->beginPin("u0"));
    submit(*service);
    const auto operation = service->session().pin_operation_id();
    step(*service, 12000);
    assert(service->pinSnapshot().place == PinSnapshot::Place::Roster);
    assert(service->session().terminal_count_for(operation) == 1);
    service->signOut(now);
}
