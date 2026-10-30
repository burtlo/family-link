# Device presentation invariants (`client/device/`)

**Status:** Normative for product firmware under `client/device/`.
**Sits between:** experience specifications (what the person sees and does) and
[device-target-invariants.md](device-target-invariants.md) (heap, stack, boot
markers). It does not restate either.

Experience specs say *“tap a card → PIN”* and *“unmute → login returns”*. They do
not say how five firmware modules share one 320 × 240 panel, one LVGL task, and
one touch controller. Every rule below exists because breaking it produced a desk
regression on 2026-10-24 (mute black screen, dead roster touch, dead PIN pad,
slow PIN pad). The rules are written so a fix to one screen cannot silently break
another.

## Model in one picture

```text
LVGL task, every 50 ms — ProductRuntime::tick() via on_product_timer
                          (the only product lv_timer; product_runtime.cpp)
│
├─ 1 INPUT        pollInput()
│                   mute latch level → finish transition, blank, sign out
│                                      | refresh hangout, Root → Login, wake
│                   BOOT / RED → current controller, only if it declares a
│                                handler and no transition is busy
├─ 2 CONNECTIVITY LoginService::flushConnectivity()   (state only)
├─ 3 SESSION      LoginService::advance()             (session + login HTTP)
├─ 4 SERVICES     serviceFeatures()                   (no feature services yet)
└─ 5 PRESENT      present()
                    pending navigation → Root replacement (animated)
                    Root's one current controller renders from snapshots
```

`RootViewController` owns exactly one live child controller. Controllers own
their LVGL trees from `loadView` to `unloadView`; services (for example
`LoginService`) live outside controllers and survive unload. The transition
coordinator's `lv_anim` is LVGL-internal and is not a product `lv_timer`.

LVGL event callbacks (taps, scrolls) run on the same task between ticks. They
post an intent to their service or a navigation request to the runtime. They
may repaint their own controller's widgets for immediate feedback. They never
replace Root's child, change display power, or touch another controller; the
next tick presents the result.

The legacy Inbox, Send, Volume, Record, and stage-router modules stay compiled
but are not loaded or attached. Any legacy attach logs `vc_construct name=<X>`,
which must never appear.

## Invariant catalog

| ID | Rule | Why (evidence) | Proof |
|----|------|----------------|-------|
| PRES-TICK | Product code creates exactly one `lv_timer`: `on_product_timer` in `product_runtime.cpp`. No view controller owns a timer. Services expose an advance/service call and are called from the tick. `lv_anim` used by transitions is not a product timer. | Separate inbox and send timers ran in an order nobody chose, re-rendered the same widgets twice per frame, and were invisible to the PRES-PATH scan. | `test_presentation_single_timer` |
| PRES-ORDER | Tick phases run in the order above. Input precedes session work so a mute in this tick blanks and signs out before stale HTTP results are applied. Present is last. | Mute sampled after HTTP let a late result repaint a signed-out session. | `test_presentation_tick_phase_order` |
| PRES-NORETURN | `ProductRuntime::tick` never returns early. | An early return while muted skipped login HTTP and stranded the device on a black screen after unmute. | `test_presentation_tick_phase_order` |
| PRES-OWNER | `RootViewController` is the only owner of what is on screen: it holds one current child and is the only object that starts appearance transitions. Navigation reaches Root only through the runtime. Active sources (`product_app_run`, runtime, Login service/controller, window, view-controller, coordinator) never reach `product_stage::`, `product_login::attach`, or legacy feature modules. While legacy modules remain compiled, `product_stage::present()` stays the only caller of their visibility setters. | Visibility was written from login render, send paint, volume layout, inbox mute, and the stage router; each writer fixed one screen and broke another. | `test_presentation_single_visibility_owner` |
| PRES-SCREEN | Exactly one controller tree is live outside a transition. During a forward transition the outgoing controller is unloaded and represented only by a static PSRAM snapshot image; two live controller trees never coexist. | Two modules visible in one slot is the root of every “touch dead” report. | `test_product_view_controller` + desk matrix |
| PRES-HIDDEN | Hiding is the only input gate, with one sanctioned exception: while a transition is busy the coordinator clears `LV_OBJ_FLAG_CLICKABLE` on the incoming root (the snapshot image is never clickable) and restores it before `viewDidAppear`. LVGL skips hidden subtrees for drawing and hit-testing, and non-clickable containers never capture a tap. Product code does not call `lv_obj_move_foreground`, `lv_obj_move_background`, or reorder siblings to win touch. Ordering items *inside* one collection (`lv_obj_move_to_index` for carousel cards) is allowed. | Three parallel “raise/park/restore” mechanisms fought each other every tick and reset presses mid-tap. | `test_presentation_no_z_order_games` |
| PRES-POINTER | `lv_indev_reset` appears once: in `wake_login_display`. Widget deletion already clears pointer state in LVGL 9 (`lv_obj_delete` resets any indev whose active object is deleted). | `lv_indev_reset` from the mute poll and the PIN render ran ~20×/s and made the PIN pad slow or dead. | `test_presentation_single_pointer_reset` |
| PRES-POWER | Display power follows the mute latch only: once at boot in `boot_backlight_after_display` (`app_main.c`; if mute is latched at boot, keep the display dark for the whole early boot path, including when UI attach never succeeds), a guarded re-assert in `product_app_run` after UI attach, then from `blank_login_display` / `wake_login_display`. One legacy writer remains: `join_configured_wifi` re-asserts backlight-on after `esp_wifi_start` when unlatched; it runs on the main task, so treat it as suspect in any blank-screen-after-boot report. Blank = `bsp_display_backlight_off()`. Wake = `bsp_display_brightness_set(80)`. Never `bsp_display_enter_sleep` / `bsp_display_exit_sleep` in product code. Bring-up sequencing is **BOOT-ORDER** in [device-target-invariants.md](device-target-invariants.md). | `exit_sleep` without a matching `enter_sleep` reconfigures BOX-3 touch INT (GPIO 3); touch stays dead until power cycle. | `test_presentation_display_power` |
| PRES-MUTE | The mute latch *level* is reconciled every tick in `ProductRuntime::pollInput`, on every controller and mid-transition. Latch: finish any busy transition, blank, sign the Login session out. Release: refresh the hangout, non-animated replacement to `LoginViewController` if it is not current, render, then wake. Both are no-ops when the level has not changed. Login is the return point from mute. | Edge-only mute missed latches; an unconditional repaint on every open-latch poll reset touch 20×/s. | `test_presentation_mute_reconcile` |
| PRES-RENDER | A controller's `render` reads service snapshots and writes only its own widgets. It never transitions a session, requests navigation, replaces Root's child, wakes the display, or calls another module. Repeated renders of the same snapshot change nothing (labels use change-detected setters; alignment is set once at attach). | Sign-in side effects lived inside `render_login_copy`; label re-set and re-align every 50 ms invalidated the header each frame. | `test_presentation_render_is_pure` |
| PRES-EDGE | Session-edge side effects run once in phase 3, not from render. In the first controller slice no sign-in edge exists: card selection navigates to the PIN-entry position (filled by `NoDestinationViewController`) without advancing the session. | Same as PRES-RENDER. | `test_presentation_render_is_pure` |
| PRES-LEVEL2 | Replacing a lower-slot panel inside one screen (roster ↔ PIN pad ↔ status copy) hides or deletes the outgoing panel in the same render that shows the incoming one. Roster cards are deleted on PIN entry; the PIN pad is hidden, not deleted, on roster entry. | A recreated roster row above a hidden PIN pad was misdiagnosed as a z-order bug; the real fault was the per-tick pointer reset. | Desk matrix |

## Change protocol for agents and reviewers

The 2026-10-24 sessions added a new mechanism per desk report and removed none.
Before adding any presentation call:

1. **Name the invariant** the change restores. If none fits, stop and propose a new
   row here first; do not encode it only in a test.
2. **Find the writer that broke it** (`rg` for the setter; PRES-OWNER means there
   should be one). Fix that writer. Do not add a second writer that “corrects”
   the first on a later tick.
3. **No per-tick compensation.** Anything that runs every tick must be idempotent
   and cheap. Raising, resetting, re-creating, or re-aligning every tick is a
   symptom mask.
4. **Hardware before software.** A “touch dead” report gets serial first
   (`make app:monitor FILTER='product_mute:I product_login:I product_stage:I'`).
   A GPIO reconfigure line on wake, or no `screen=` change, points at a different
   layer than LVGL.
5. **Guardrails test invariants, not helper names.** A test asserting that
   `foo_helper()` exists legitimizes the patch that added it. Assert the
   invariant (one owner, one timer, no z-order calls) instead.
6. **One failure class per change** ([standards §12](client-application-coding-standards.md#12-change-discipline)).
   Presentation lifecycle, login session/HTTP, and BSP/display power are three
   different classes.

## Desk matrix (L3)

A presentation change is not *behavior*-class
([claim classes](device-target-invariants.md#claim-classes)) until this matrix
passes on hardware with serial saved. Each row starts from a fresh boot unless
stated.

While only Root, Login, and the empty destination exist, run the first-slice
rows below. The full matrix returns as PIN, Inbox, Volume, and Send controllers
are added.

| # | Steps | Expect |
|---|-------|--------|
| S1 | Boot unmuted → wait for roster → tap a card | 400 ms cover-up to an empty surface; `vc_unload name=Login` before `vc_load name=NoDestination` |
| S2 | Empty destination → MUTE on → MUTE off | Backlight off, then roster; no `vc_transition_begin`; card taps work |
| S3 | Tap a card → MUTE on during the cover → off | Transition ends; roster after unmute |
| S4 | Roster and empty destination → BOOT, RED | Nothing happens |
| S5 | Boot with MUTE latched → unlatch | Dark through attach; then roster; taps work |

Full matrix (requires later controllers):

| # | Steps | Expect |
|---|-------|--------|
| 1 | Boot unmuted → wait for roster → tap a card | PIN pad; digits respond on first tap |
| 2 | Enter 4 wrong digits | `checking...` → `wrong pin` 2 s → keypad accepts digits again |
| 3 | Enter correct PIN | Inbox carousel; `screen=1` in serial |
| 4 | Inbox → MUTE on | Backlight off; `screen=0` |
| 5 | MUTE off | `connecting` → roster; tap card → PIN responds; enter PIN → inbox |
| 6 | Roster → MUTE on → MUTE off (×3) | Roster each time; card taps work each time |
| 7 | PIN pad with 2 digits → MUTE on → off | Roster (digits discarded); PIN pad responds after re-select |
| 8 | Boot with MUTE latched → unlatch | Backlight on; roster; taps work |
| 9 | Inbox → BOOT (volume) → BOOT | Volume slider, then inbox; carousel swipes work |
| 10 | Inbox → RED (picker) → BOOT | Picker, then inbox; carousel swipes work |

## Where the checks live

| Check | Location |
|-------|----------|
| Source invariants above | `scripts/device_guardrails/presentation_invariants.py`, run by `scripts/tests/test_device_guardrails.py` |
| Timer-path blocking (PRES-PATH) | `scripts/device_guardrails/presentation_path.py`, rooted at `on_product_timer` |
| Controller lifecycle, transition, mute and button dispatch | `scripts/tests/test_product_view_controller.py` (real LVGL on host, `scripts/tests/view_controller_host/`) |
| Screen-level invariants | Each `docs/product/device-ui/screens/SCR-*.md` § Implementation invariants |
