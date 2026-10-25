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
LVGL task, every 50 ms — on_login_timer() (the only lv_timer in product code)
│
├─ 1 INPUT        product_hardware_buttons_poll()
│                   BOOT / RED intents → stage router (state only)
│                   mute latch level   → product_login_on_mute_level (state + backlight)
├─ 2 CONNECTIVITY flush_wifi_schedule()          (state only)
├─ 3 SESSION      login session advance, drain, login HTTP results
│                   note_sign_in_edge()          (sign-in side effects, once)
├─ 4 SERVICES     inbox (signed in) | discard inbox results (signed out)
│                   volume, send, record         (state + own widget updates)
└─ 5 PRESENT      product_stage_present()
                    screen = f(signed_in, shell stage)
                    login / inbox / volume / send / record ← set visible(screen == X)
                    visible module renders from its session snapshot
```

LVGL event callbacks (taps, scrolls, slider) run on the same task between ticks.
They post an intent to their own session. They may repaint their own module's
widgets for immediate feedback. They never change visibility, the shell stage of
another module, or display power; the next tick presents the result.

## Invariant catalog

| ID | Rule | Why (evidence) | Proof |
|----|------|----------------|-------|
| PRES-TICK | Product code creates exactly one `lv_timer`: `on_login_timer`. Feature modules expose `*_service(now_ms)` and are called from it. | Separate inbox and send timers ran in an order nobody chose, re-rendered the same widgets twice per frame, and were invisible to the PRES-PATH scan. | `test_presentation_single_timer` |
| PRES-ORDER | Tick phases run in the order above. Input precedes session work so a mute in this tick blanks and signs out before stale HTTP results are applied. Present is last. | Mute sampled after HTTP let a late result repaint a signed-out session. | `test_presentation_tick_phase_order` |
| PRES-NORETURN | `on_login_timer` never returns early. | An early return while muted skipped login HTTP and stranded the device on a black screen after unmute. | `test_presentation_tick_phase_order` |
| PRES-OWNER | `product_stage_present()` is the only caller of `product_login_present`, `product_inbox_set_ui_visible`, `product_volume_set_visible`, `product_send_present`, and `product_record_set_visible`. Feature modules never show or hide another module. | Visibility was written from login render, send paint, volume layout, inbox mute, and the stage router; each writer fixed one screen and broke another. | `test_presentation_single_visibility_owner` |
| PRES-SCREEN | Exactly one screen is active per tick: `LOGIN` when signed out; otherwise `VOLUME`, `RECORD`, `SEND` (picker, receipt, couldn't-record), or `INBOX` by shell stage. A module's widgets are visible only on its screen. | Two modules visible in one slot is the root of every “touch dead” report. | `product_stage_screen()` + desk matrix |
| PRES-HIDDEN | Hiding is the only input gate. LVGL skips hidden subtrees for drawing and hit-testing, and non-clickable containers never capture a tap. Product code does not call `lv_obj_move_foreground`, `lv_obj_move_background`, or reorder siblings to win touch. Ordering items *inside* one collection (`lv_obj_move_to_index` for carousel cards) is allowed. | Three parallel “raise/park/restore” mechanisms fought each other every tick and reset presses mid-tap. | `test_presentation_no_z_order_games` |
| PRES-POINTER | `lv_indev_reset` appears once: in `wake_login_display`. Widget deletion already clears pointer state in LVGL 9 (`lv_obj_delete` resets any indev whose active object is deleted). | `lv_indev_reset` from the mute poll and the PIN render ran ~20×/s and made the PIN pad slow or dead. | `test_presentation_single_pointer_reset` |
| PRES-POWER | Display power follows the mute latch only: once at boot in `app_main.c` (latched boot never sets a duty), then from `blank_login_display` / `wake_login_display`. One legacy writer remains: `join_configured_wifi` re-asserts backlight-on after `esp_wifi_start` when unlatched; it runs on the main task, so treat it as suspect in any blank-screen-after-boot report. Blank = `bsp_display_backlight_off()`. Wake = `bsp_display_brightness_set(80)`. Never `bsp_display_enter_sleep` / `bsp_display_exit_sleep` in product code. | `exit_sleep` without a matching `enter_sleep` reconfigures BOX-3 touch INT (GPIO 3); touch stays dead until power cycle. | `test_presentation_display_power` |
| PRES-MUTE | The mute latch *level* is reconciled every tick (`enter_muted` / `leave_muted`). Each is a no-op when display power and session already agree. Neither renders. | Edge-only mute missed latches; an unconditional repaint on every open-latch poll reset touch 20×/s. | `test_presentation_mute_reconcile` |
| PRES-RENDER | A render function reads session snapshots and writes only its own widgets. It never transitions a session, sets the shell stage, wakes the display, or calls another module. Repeated renders of the same snapshot change nothing (labels use change-detected setters; alignment is set once at attach). | Sign-in side effects lived inside `render_login_copy`; label re-set and re-align every 50 ms invalidated the header each frame. | `test_presentation_render_is_pure` |
| PRES-EDGE | Session-edge side effects (sign-in: inbox boot, send identity, shell → `INBOX`) run once in phase 3 from `note_sign_in_edge`, not from render. | Same as PRES-RENDER. | `test_presentation_render_is_pure` |
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
| Timer-path blocking (PRES-PATH) | `scripts/device_guardrails/presentation_path.py` — now also reaches send, because send is serviced from the one tick |
| Screen-level invariants | Each `docs/product/device-ui/screens/SCR-*.md` § Implementation invariants |
