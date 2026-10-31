#include "product_view_controller.hpp"
#include "product_split_pane_view_controller.hpp"
#include "product_login_view_controller.hpp"
#include "product_runtime.hpp"
#include "product_theme.hpp"
#include <cassert>
#include <cstdlib>
#include <cstring>
#include <string>
#include <vector>

using namespace product_ui;
static uint32_t ticks;
static uint32_t clockTick()
{
    return ticks;
}
void advancePinTestClock(uint32_t milliseconds)
{
    ticks += milliseconds;
}
static int allocations;
static size_t requested;
static bool failAllocate;
static bool failImage;
static void *allocate(size_t bytes)
{
    requested = bytes;
    if (failAllocate)
        return nullptr;
    ++allocations;
    return std::malloc(bytes);
}
static void release(void *p)
{
    --allocations;
    std::free(p);
}
static lv_obj_t *createImage(lv_obj_t *parent)
{
    return failImage ? nullptr : lv_image_create(parent);
}
static void flush(lv_display_t *display, const lv_area_t *, uint8_t *)
{
    lv_display_flush_ready(display);
}
static std::vector<std::string> events;
class Recording : public ViewController
{
  public:
    explicit Recording(const char *id, bool fail = false) : id_(id), fail_(fail) {}
    const char *name() const override
    {
        return id_;
    }
    void onBoot() override
    {
        assert(false && "undeclared BOOT handler called");
    }
    void onRed() override
    {
        assert(false && "undeclared RED handler called");
    }

  protected:
    bool createView(lv_obj_t *parent) override
    {
        events.push_back(std::string(id_) + ":load");
        return !fail_ && ViewController::createView(parent);
    }
    void viewDidLoad() override
    {
        record("didLoad");
    }
    void viewWillAppear() override
    {
        record("willAppear");
    }
    void viewDidAppear() override
    {
        record("didAppear");
    }
    void viewWillDisappear() override
    {
        assert(view());
        record("willDisappear");
    }
    void viewDidDisappear() override
    {
        assert(!view());
        record("didDisappear");
    }
    void viewWillUnload() override
    {
        record("unload");
    }

  private:
    void record(const char *s)
    {
        events.push_back(std::string(id_) + ":" + s);
    }
    const char *id_;
    bool fail_;
};

class Oversized : public ViewController
{
  public:
    const char *name() const override
    {
        return "oversized";
    }

  protected:
    bool createView(lv_obj_t *parent) override
    {
        if (!ViewController::createView(parent))
            return false;
        lv_obj_set_width(root_, 400); // Redraw cannot fit the fixed snapshot buffer.
        return true;
    }
};

class PartialFailure : public ViewController
{
  public:
    const char *name() const override
    {
        return "partial";
    }

  protected:
    bool createView(lv_obj_t *parent) override
    {
        if (!ViewController::createView(parent))
            return false;
        child_ = lv_obj_create(root_);
        assert(child_);
        return false;
    }
    void viewWasRemoved() override
    {
        child_ = nullptr;
    }

  private:
    lv_obj_t *child_ = nullptr;
};

class Reentrant : public ViewController
{
  public:
    explicit Reentrant(RootViewController &root) : owner_(root) {}
    const char *name() const override
    {
        return "reentrant";
    }
    int didLoads = 0;
    int rejected = 0;
    int bootCalls = 0;
    bool handlesBoot() const override
    {
        return true;
    }
    void onBoot() override
    {
        ++bootCalls;
    }

  protected:
    bool createView(lv_obj_t *parent) override
    {
        if (!ViewController::createView(parent))
            return false;
        child_ = lv_obj_create(root_);
        return child_ != nullptr;
    }
    void viewDidLoad() override
    {
        ++didLoads;
        rejectNavigation();
    }
    void viewWillAppear() override
    {
        rejectNavigation();
    }
    void viewDidAppear() override
    {
        rejectNavigation();
    }
    void viewWillDisappear() override
    {
        rejectNavigation();
    }
    void viewDidDisappear() override
    {
        assert(!view() && !child_);
        rejectNavigation();
    }
    void viewWillUnload() override
    {
        rejectNavigation();
    }
    void viewWasRemoved() override
    {
        child_ = nullptr;
    }

  private:
    void rejectNavigation()
    {
        assert(owner_.busy());
        assert(!owner_.replace(std::unique_ptr<ViewController>(new NoDestinationViewController)));
        owner_.finishTransition(); // No effect while building/completing a transaction.
        ++rejected;
    }
    RootViewController &owner_;
    lv_obj_t *child_ = nullptr;
};

class LoadCounter : public ViewController
{
  public:
    const char *name() const override
    {
        return "counter";
    }
    int loads = 0;

  protected:
    void viewDidLoad() override
    {
        ++loads;
    }
};

class SlotFailure : public SplitPaneViewController
{
  public:
    explicit SlotFailure(int step) : failAt_(step) {}

  protected:
    lv_obj_t *createSlot(lv_obj_t *parent) override
    {
        return ++calls_ == failAt_ ? nullptr : SplitPaneViewController::createSlot(parent);
    }

  private:
    int failAt_;
    int calls_ = 0;
};

static void testSplitPane(lv_obj_t *content)
{
    const auto initialChildren = lv_obj_get_child_count(content);
    // LVGL lazily allocates the parent's child-list metadata on first use.
    // Warm that parent-owned allocation before measuring controller recovery.
    auto *warm = lv_obj_create(content);
    lv_obj_delete(warm);
    lv_mem_monitor_t before{}, after{};
    lv_mem_monitor(&before);
    SplitPaneViewController pane;
    assert(pane.loadView(content));
    lv_obj_update_layout(pane.view());
    assert(lv_obj_get_width(pane.view()) == 320 && lv_obj_get_height(pane.view()) == 240);
    assert(lv_obj_get_child_count(pane.view()) == 2);
    assert(lv_obj_get_parent(pane.upperSlot()) == pane.view());
    assert(lv_obj_get_parent(pane.lowerSlot()) == pane.view());
    assert(lv_obj_get_x(pane.upperSlot()) == 0 && lv_obj_get_y(pane.upperSlot()) == 0);
    assert(lv_obj_get_width(pane.upperSlot()) == 320 && lv_obj_get_height(pane.upperSlot()) == 80);
    assert(lv_obj_get_x(pane.lowerSlot()) == 0 && lv_obj_get_y(pane.lowerSlot()) == 80);
    assert(lv_obj_get_width(pane.lowerSlot()) == 320 && lv_obj_get_height(pane.lowerSlot()) == 160);
    assert(lv_obj_get_child_count(pane.upperSlot()) == 0 && lv_obj_get_child_count(pane.lowerSlot()) == 0);
    assert(!lv_obj_is_scrollable(pane.upperSlot()) && !lv_obj_is_scrollable(pane.lowerSlot()));
    assert(!lv_obj_is_clickable(pane.upperSlot()) && !lv_obj_is_clickable(pane.lowerSlot()));
    assert(!lv_obj_is_overflow_visible(pane.upperSlot()) && !lv_obj_is_overflow_visible(pane.lowerSlot()));
    assert(lv_color_eq(lv_obj_get_style_bg_color(pane.upperSlot(), LV_PART_MAIN),
                       lv_color_hex(product_theme::Dark::kSurfaceUpperRgb24)));
    assert(lv_color_eq(lv_obj_get_style_bg_color(pane.lowerSlot(), LV_PART_MAIN),
                       lv_color_hex(product_theme::Dark::kSurfaceLowerRgb24)));
    auto upperBounds = pane.upperContentBounds();
    auto lowerBounds = pane.lowerContentBounds();
    assert(upperBounds.x == 12 && upperBounds.y == 12 && upperBounds.width == 296 && upperBounds.height == 56);
    assert(lowerBounds.x == 12 && lowerBounds.y == 12 && lowerBounds.width == 296 && lowerBounds.height == 136);
    pane.unloadView();
    assert(!pane.view() && !pane.upperSlot() && !pane.lowerSlot());
    assert(lv_obj_get_child_count(content) == initialChildren);
    pane.unloadView();
    assert(pane.loadView(content) && pane.upperSlot() && pane.lowerSlot());
    pane.unloadView();
    for (int step : {1, 2}) {
        SlotFailure failing(step);
        assert(!failing.loadView(content));
        assert(!failing.loaded() && !failing.view() && !failing.upperSlot() && !failing.lowerSlot());
        assert(lv_obj_get_child_count(content) == initialChildren);
        assert(failing.loadView(content));
        assert(failing.upperSlot() && failing.lowerSlot());
        failing.unloadView();
    }
    std::vector<void *> exhausted;
    while (void *block = lv_malloc(64))
        exhausted.push_back(block);
    SplitPaneViewController failingRoot;
    assert(!failingRoot.loadView(content));
    assert(!failingRoot.view() && !failingRoot.upperSlot() && !failingRoot.lowerSlot());
    for (void *block : exhausted)
        lv_free(block);
    lv_mem_monitor(&after);
    assert(before.free_size == after.free_size);
    assert(lv_obj_get_child_count(content) == initialChildren);
}

static bool deliverRoster;
static int rosterCount;
static int transportTicks;
static int muteCalls;
static bool startLogin(fl_product_login::SessionWriter *)
{
    return true;
}
static void advanceLogin(fl_product_login::SessionWriter *session, int64_t now)
{
    ++transportTicks;
    if (!deliverRoster || !session->roster_inflight())
        return;
    fl_product_login::RosterResult roster{};
    roster.operation_id = session->roster_operation_id();
    roster.http_status = 200;
    roster.valid = 1;
    roster.parse_ok = 1;
    roster.user_count = rosterCount;
    for (int i = 0; i < rosterCount; ++i) {
        std::snprintf(roster.users[i].id, sizeof(roster.users[i].id), "u%d", i);
        std::snprintf(roster.users[i].display_name, sizeof(roster.users[i].display_name), "User %d", i);
        std::strcpy(roster.users[i].card_background_id, "card-bg-02");
    }
    session->deliver_roster_completion(roster, now);
}
static void muteLogin()
{
    ++muteCalls;
}
struct NavigationRecord {
    int count = 0;
    std::string user;
    LoginService *service = nullptr;
    LoginViewController *controller = nullptr;
};
static void navigate(void *context, LoginDestination destination, const char *user)
{
    assert(destination == LoginDestination::PinEntry);
    auto *record = static_cast<NavigationRecord *>(context);
    assert(std::strcmp(record->service->snapshot().focus, user) == 0);
    const auto snapshot = record->service->snapshot();
    for (int i = 0; i < snapshot.count; ++i) {
        const lv_opa_t expected = std::strcmp(snapshot.users[i].id, user) == 0 ? LV_OPA_COVER : LV_OPA_60;
        assert(lv_obj_get_style_bg_opa(record->controller->cardAt(i), LV_PART_MAIN) == expected);
        if (std::strcmp(snapshot.users[i].id, user) == 0) {
            assert(lv_obj_get_scroll_x(record->controller->rosterRow()) == i * 144);
        }
    }
    ++record->count;
    record->user = user;
}
static void testLogin(lv_obj_t *content)
{
    auto service = std::unique_ptr<LoginService>(new LoginService({startLogin, advanceLogin, muteLogin}));
    service->scheduleWifi(true); // Network notifications may arrive before boot.
    service->boot(0, false);
    service->flushConnectivity(0);
    service->advance(0);
    assert(service->snapshot().body == LoginSnapshot::Body::Connecting);
    NavigationRecord navigation;
    LoginViewController login(*service, {&navigation, navigate});
    navigation.service = service.get();
    navigation.controller = &login;
    assert(login.loadView(content));
    login.render();
    assert(std::strcmp(lv_label_get_text(login.heading()), "Loading hangout ...") == 0);
    assert(std::strstr(lv_label_get_text(login.statusLabel()), "connecting") != nullptr);
    service->scheduleWifi(false);
    service->flushConnectivity(1);
    service->advance(1);
    login.render();
    assert(std::strcmp(lv_label_get_text(login.statusLabel()), "no Wi-Fi") == 0);
    service->scheduleWifi(true);
    service->flushConnectivity(2);
    rosterCount = 0;
    deliverRoster = true;
    service->advance(2);
    login.render();
    assert(service->snapshot().body == LoginSnapshot::Body::Empty);
    assert(std::strcmp(lv_label_get_text(login.heading()), "Sign in as ...") == 0);
    assert(std::strcmp(lv_label_get_text(login.statusLabel()), "No people in this hangout") == 0);
    deliverRoster = false;
    const uint64_t healthBefore = service->session().health_operation_id();
    service->scheduleWifi(false); // A drop and recovery inside one tick still reaches the session.
    service->scheduleWifi(true);
    service->flushConnectivity(3);
    assert(service->session().health_operation_id() != healthBefore);
    deliverRoster = true;
    service->advance(3);
    assert(service->snapshot().body == LoginSnapshot::Body::Empty);
    login.unloadView();
    assert(!login.heading() && !login.statusLabel() && !login.rosterRow());
    assert(!login.selectUser(0, "u0"));
    service->signOut(3);
    rosterCount = 2;
    service->refreshHangout(4);
    const int ticksBefore = transportTicks;
    service->advance(4); // No loaded controller; service must still apply roster.
    assert(transportTicks > ticksBefore && service->snapshot().count == 2);
    assert(login.loadView(content));
    login.render();
    assert(login.cardAt(0) && login.cardAt(1) && !login.cardAt(2));
    auto *first = login.cardAt(0);
    const unsigned revision = login.widgetRevision();
    login.render();
    assert(login.widgetRevision() == revision && login.cardAt(0) == first);
    assert(!login.selectUser(-1, "u0") && !login.selectUser(8, "u0"));
    assert(!login.selectUser(0, "stale"));
    // Focus remains u0 while browsing to u1; tapping u0 must center it before
    // navigation even though its focus identity did not change.
    lv_obj_update_layout(login.view());
    lv_obj_scroll_to_x(login.rosterRow(), 144, LV_ANIM_OFF);
    assert(login.selectUser(0, "u0"));
    assert(navigation.count == 1 && navigation.user == "u0");
    assert(lv_obj_get_scroll_x(login.rosterRow()) == 0);
    login.unloadView();
    assert(login.loadView(content));
    login.render();
    navigation.count = 0;
    first = login.cardAt(0);
    lv_obj_update_layout(login.view());
    lv_obj_send_event(first, LV_EVENT_PRESSED, nullptr);
    lv_obj_scroll_to_x(login.rosterRow(), 144, LV_ANIM_OFF);
    lv_obj_send_event(login.rosterRow(), LV_EVENT_SCROLL, nullptr);
    lv_obj_send_event(first, LV_EVENT_CLICKED, nullptr);
    assert(navigation.count == 0);
    auto *second = login.cardAt(1);
    lv_obj_send_event(second, LV_EVENT_PRESSED, nullptr);
    lv_obj_send_event(second, LV_EVENT_CLICKED, nullptr);
    assert(navigation.count == 1 && navigation.user == "u1");
    lv_obj_send_event(second, LV_EVENT_CLICKED, nullptr);
    assert(navigation.count == 1);
    assert(service->session().pin_phase() == fl_product_login::kPinPhaseIdle);
    assert(!service->session().signed_in());
    login.unloadView();
    assert(!login.selectUser(1, "u1"));
    assert(login.loadView(content));
    login.render();
    lv_obj_update_layout(login.view());
    assert(std::strcmp(service->snapshot().focus, "u1") == 0);
    assert(lv_obj_get_scroll_x(login.rosterRow()) == 144);
    assert(lv_obj_get_style_bg_opa(login.cardAt(1), LV_PART_MAIN) == LV_OPA_COVER);
    login.unloadView();
    service->signOut(5);
    assert(service->snapshot().muted && muteCalls >= 2);
    service->refreshHangout(6);
    service->advance(6);
    assert(service->snapshot().body == LoginSnapshot::Body::Roster);
    service->signOut(7);
    rosterCount = 8;
    service->refreshHangout(8);
    service->advance(8);
    assert(login.loadView(content));
    login.render();
    for (int i = 0; i < 8; ++i)
        assert(login.cardAt(i));
    assert(!login.cardAt(8));
    assert(lv_obj_get_child_count(login.rosterRow()) == 8);
    const auto eightRevision = login.widgetRevision();
    login.render();
    assert(login.widgetRevision() == eightRevision);
    for (int64_t now = 258; now <= 600258; now += 250)
        service->advance(now);
    assert(service->session().roster_period_ms() == fl_product_login::kIdleRosterPeriodMs);
    login.render();
    assert(service->session().roster_period_ms() == fl_product_login::kIdleRosterPeriodMs);
    // Browsing is user activity even without selecting/navigating.
    lv_obj_update_layout(login.view());
    lv_obj_send_event(login.cardAt(0), LV_EVENT_PRESSED, nullptr);
    lv_obj_scroll_to_x(login.rosterRow(), 288, LV_ANIM_OFF);
    lv_obj_send_event(login.rosterRow(), LV_EVENT_SCROLL, nullptr);
    assert(service->session().roster_period_ms() == fl_product_login::kActiveRosterPeriodMs);
    assert(service->session().next_roster_due_ms() <= service->session().now_ms() + fl_product_login::kActiveRosterPeriodMs);
    login.unloadView();
}

static bool bootPressed;
static bool redPressed;
static int unlatched()
{
    return 0;
}
static bool takeBoot()
{
    const bool p = bootPressed;
    bootPressed = false;
    return p;
}
static bool takeRed()
{
    const bool p = redPressed;
    redPressed = false;
    return p;
}

static void testRuntimeButtons(lv_obj_t *content)
{
    LoginService service({startLogin, advanceLogin, muteLogin});
    RootViewController root(content, {allocate, release, createImage});
    RuntimePlatform platform;
    platform.muteLatched = unlatched;
    platform.takeBoot = takeBoot;
    platform.takeRed = takeRed;
    ProductRuntime runtime(root, service, platform);
    assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
    bootPressed = redPressed = true;
    runtime.tick(0);
    assert(!bootPressed && !redPressed);
    auto *handler = new Reentrant(root);
    assert(root.replace(std::unique_ptr<ViewController>(handler), false));
    runtime.tick(50);
    assert(handler->bootCalls == 0);
    bootPressed = true;
    runtime.tick(100);
    assert(handler->bootCalls == 1);
    assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
}

void testPinControllers(lv_obj_t *content);
void testInboxControllers(lv_obj_t *content);
void testPlaybackService(lv_obj_t *content);
void testSettingsControllers(lv_obj_t *content);
void testMessageTargetControllers(lv_obj_t *content);
void testRuntimeRoutes(lv_obj_t *content);
void testRepairGates(lv_obj_t *content);
void testRecordSend(lv_obj_t *content);

int main()
{
    lv_init();
    lv_tick_set_cb(clockTick);
    lv_timer_set_period(lv_anim_get_timer(), 1);
    auto *display = lv_display_create(320, 240);
    static uint8_t stripe[320 * 16 * 2];
    lv_display_set_color_format(display, LV_COLOR_FORMAT_RGB565);
    lv_display_set_buffers(display, stripe, nullptr, sizeof(stripe), LV_DISPLAY_RENDER_MODE_PARTIAL);
    lv_display_set_flush_cb(display, flush);
    auto *content = lv_obj_create(lv_screen_active());
    lv_obj_remove_style_all(content);
    lv_obj_set_size(content, 320, 240);
    testSplitPane(content);
    testLogin(content);
    testPinControllers(content);
    testInboxControllers(content);
    testPlaybackService(content);
    testSettingsControllers(content);
    testMessageTargetControllers(content);
    testRecordSend(content);
    {
        LoadCounter controller;
        assert(controller.loadView(content) && controller.loads == 1);
        assert(controller.loadView(content) && controller.loads == 1);
        controller.unloadView();
        assert(controller.loadView(content) && controller.loads == 2);
        controller.unloadView();
    }
    {
        RootViewController root(content, {allocate, release, createImage});
        auto *reentrant = new Reentrant(root);
        assert(root.replace(std::unique_ptr<ViewController>(reentrant), false));
        assert(reentrant->didLoads == 1 && reentrant->rejected == 3);
        root.dispatchBoot();
        assert(reentrant->bootCalls == 1);
        failImage = true;
        assert(!root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController)));
        assert(root.current() == reentrant && reentrant->loaded() && reentrant->appeared());
        assert(!root.busy() && !root.snapshotImage() && allocations == 0);
        failImage = false;
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        // Reentrant didDisappear verifies all derived handles were cleared.
        auto *incoming = new Reentrant(root);
        assert(root.replace(std::unique_ptr<ViewController>(incoming)));
        assert(root.busy());
        assert(!root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController)));
        root.dispatchBoot();
        assert(incoming->bootCalls == 0);
        root.finishTransition();
        assert(incoming->appeared() && incoming->rejected == 3);
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        const uint32_t children = lv_obj_get_child_count(content);
        assert(!root.replace(std::unique_ptr<ViewController>(new PartialFailure), false));
        assert(!root.current() && allocations == 0);
        assert(lv_obj_get_child_count(content) == children - 1);
    }
    {
        NoDestinationViewController controller;
        assert(controller.loadView(content));
        controller.unloadView();
        controller.unloadView();
        assert(!controller.loaded() && !controller.view());
        assert(controller.loadView(content));
        controller.unloadView();
    }
    {
        RootViewController root(content, {allocate, release, createImage});
        assert(root.replace(std::unique_ptr<ViewController>(new Recording("old")), false));
        events.clear();
        assert(root.replace(std::unique_ptr<ViewController>(new Recording("new")), false));
        assert((events == std::vector<std::string>{"old:willDisappear", "old:didDisappear", "old:unload",
                                                   "new:load", "new:didLoad", "new:willAppear", "new:didAppear"}));
        events.clear();
        assert(root.replace(std::unique_ptr<ViewController>(new Recording("finish"))));
        assert((events == std::vector<std::string>{"new:willDisappear", "new:didDisappear", "new:unload",
                                                   "finish:load", "finish:didLoad", "finish:willAppear"}));
        root.finishTransition();
        assert(events.back() == "finish:didAppear");
        assert(!root.snapshotImage() && allocations == 0 && !root.busy());
    }
    events.clear();
    {
        RootViewController root(content, {allocate, release});
        assert(root.replace(std::unique_ptr<ViewController>(new Recording("a"))));
        assert(!root.busy());
        root.dispatchBoot();
        root.dispatchRed();
        assert((events == std::vector<std::string>{"a:load", "a:didLoad", "a:willAppear", "a:didAppear"}));
        events.clear();
        failAllocate = true;
        auto *same = root.current();
        assert(!root.replace(std::unique_ptr<ViewController>(new Recording("b"))));
        assert(root.current() == same && same->loaded() && same->appeared());
        assert((events == std::vector<std::string>{"a:willDisappear", "a:willAppear", "a:didAppear"}));
        failAllocate = false;
        events.clear();
        lv_mem_monitor_t before{}, during{};
        lv_mem_monitor(&before);
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController)));
        assert(root.busy());
        lv_mem_monitor(&during);
        assert(allocations == 1 && requested >= 153600 && requested < 153600 + LV_DRAW_BUF_ALIGN);
        assert(before.free_size - during.free_size < 153600);
        assert(root.snapshotImage() && lv_obj_get_parent(root.snapshotImage()) == content);
        assert((events == std::vector<std::string>{"a:willDisappear", "a:didDisappear", "a:unload"}));
        auto *incoming = root.current()->view();
        lv_obj_update_layout(incoming);
        assert(lv_obj_get_y(incoming) == 240);
        assert(lv_obj_get_width(incoming) == 320 && lv_obj_get_height(incoming) == 240);
        assert(lv_obj_get_child_count(incoming) == 0);
        assert(lv_obj_get_style_bg_opa(incoming, LV_PART_MAIN) == LV_OPA_COVER);
        assert(lv_color_eq(lv_obj_get_style_bg_color(incoming, LV_PART_MAIN), lv_color_hex(product_theme::Dark::kSurfaceRgb24)));
        assert(!root.current()->appeared());
        for (int i = 0; i < 39; ++i) {
            ticks += 10;
            lv_timer_handler();
            lv_obj_update_layout(incoming);
            if (i == 9)
                assert(lv_obj_get_y(incoming) >= 235 && lv_obj_get_y(incoming) <= 238);
            if (i == 19)
                assert(lv_obj_get_y(incoming) == 120);
            if (i == 29)
                assert(lv_obj_get_y(incoming) >= 3 && lv_obj_get_y(incoming) <= 5);
        }
        assert(root.busy() && !root.current()->appeared());
        ticks += 10;
        lv_timer_handler();
        lv_obj_update_layout(incoming);
        assert(!root.busy() && root.current()->appeared());
        assert(!root.snapshotImage() && allocations == 0);
        assert(lv_obj_get_y(incoming) == 0);
        assert(root.replace(std::unique_ptr<ViewController>(new Recording("b"))));
        root.finishTransition();
        assert(!root.busy() && root.current()->appeared() && allocations == 0);
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        assert(!root.busy() && allocations == 0);
        assert(root.replace(std::unique_ptr<ViewController>(new Oversized), false));
        auto *oversized = root.current();
        assert(!root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController)));
        assert(root.current() == oversized && oversized->loaded() && oversized->appeared());
        assert(!root.snapshotImage() && allocations == 0);
        assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        assert(!root.replace(std::unique_ptr<ViewController>(new Recording("fail", true))));
        assert(!root.current() && allocations == 0);
        std::vector<void *> exhausted;
        while (void *block = lv_malloc(64))
            exhausted.push_back(block);
        assert(!root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
        assert(!root.current());
        for (void *block : exhausted)
            lv_free(block);
        lv_mem_monitor_t baseline{}, recovered{};
        lv_mem_monitor(&baseline);
        for (int i = 0; i < 30; ++i) {
            assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController), false));
            assert(root.replace(std::unique_ptr<ViewController>(new NoDestinationViewController)));
            root.finishTransition();
            assert(!root.snapshotImage() && allocations == 0);
        }
        lv_mem_monitor(&recovered);
        // One live destination remains, bounded independently of transition count.
        assert(baseline.free_size - recovered.free_size < 2048);
    }
    testRuntimeButtons(content);
    testRuntimeRoutes(content);
    testRepairGates(content);
    lv_obj_delete(content);
    lv_display_delete(display);
    lv_deinit();
}
