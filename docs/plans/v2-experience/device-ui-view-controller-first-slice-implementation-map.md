# Device UI view-controller first slice — implementation map

Date: 2026-10-29  
Status: **Ready** — owner decisions recorded in [Owner decisions](#owner-decisions)  
Specifications:
[`device-ui-shell-architecture-proposal.md`](device-ui-shell-architecture-proposal.md),
[`device-ui-view-controller-memory-envelope.md`](device-ui-view-controller-memory-envelope.md)  
Repository: `/Users/lynnfrank/src/family-link`  
Map scope: root, empty destination, split-pane, and Login view controllers;
product cutover; host verification; first device acceptance

## Summary

This slice replaces the existing eager UI boot path with the first vertical
view-controller implementation. It creates and tests `RootViewController`,
`NoDestinationViewController`, `SplitPaneViewController`, and
`LoginViewController`; reduces `UiWindow` to window/content-root ownership;
boots into Login; and routes a selected roster card to the empty destination.

This slice does not measure or optimize the current eager collection of Login,
PIN, Inbox, Send, Volume, and Record views. Measurement begins after the new
root and Login hierarchy are installed. The 32 KiB LVGL setting remains
unchanged.

Forward navigation uses the existing owner-defined transition identified in
the repository as `TRANSITION-COVER-DOWN`. Its forward visual motion is the
CoverUp behavior: the incoming root moves from `y=+240` to `y=0` over 400 ms
with ease-in-out quint. To avoid retaining two live controller trees, the
outgoing presentation is captured as a static transition image, the outgoing
controller is unloaded, and the incoming controller animates over that image.

## Locked decisions

- `RootViewController` is the container and the only object that starts a child
  appearance transition. The base `ViewController` implements
  `beginAppearanceTransition(appearing, animated)` / `endAppearanceTransition()`
  and dispatches the matching `will` / `did` callbacks. No slice code calls
  `viewWillAppear`, `viewWillDisappear`, `viewDidAppear`, or
  `viewDidDisappear` directly.
- Login's replacement request carries the selected user to Root. Login does
  not capture pixels, choose motion, or invoke appearance methods.
- Root uses a transition coordinator for one replacement: snapshot, removal,
  cover animation, and appearance brackets. Root remains the sole owner of the
  current child; the coordinator executes Root's container operation.
- `SplitPaneViewController` owns slots, not child controllers, in this slice.
  It does not forward appearance. Only Root's one child receives callbacks.
- Do not add `viewIsAppearing`; the coordinator places the incoming root at
  `y=+240` itself.

- `RootViewController` is valid with no child and displays nothing.
- `NoDestinationViewController` is the temporary first destination and
  has no labels, controls, timers, feature services, or slot chrome. Its loaded
  view is one opaque full-screen root with no children.
- Preserve the current boot: storage, microphone, display, backlight, NVS,
  ready marker, and Wi-Fi ordering.
- Preserve the server roster, connecting, and `no Wi-Fi` presentation inside
  `LoginViewController`.
- `SplitPaneViewController` is a view-controller base/container, not a global
  desktop host.
- `LoginViewController` extends `SplitPaneViewController`.
- Selecting a roster card navigates forward to the PIN entry position, filled
  in this slice by `NoDestinationViewController`. `PinEntryViewController` is
  outside this slice (`D-1`).
- Mute release always returns to `LoginViewController` through a non-animated
  replacement performed while the display is dark (`D-2`).
- No transition requires two live controller trees.
- Forward navigation uses the existing 400 ms CoverUp motion specified by
  `TRANSITION-COVER-DOWN` with its defined geometry and easing.
- The 112.5 KiB sketch arena remains lazily allocated. It is unused by this
  slice.
- The LVGL pool remains 32 KiB. Its size is reconsidered only from measurements
  of the replacement architecture as controllers are added.
- Storage-full behavior is deferred to a later boot/storage-health design.
- The existing `product_mem` mechanism remains and gains controller-boundary
  probes.
- The single product tick (today `on_login_timer` inside the Login bridge)
  survives Login unload. It moves to an app-level runtime owner that Root and
  controllers do not own. See [Runtime ownership after cutover](#runtime-ownership-after-cutover).
- All controller, coordinator, and snapshot calls run on the LVGL task (LVGL
  event callbacks or the product tick). Boot-time installation runs under the
  existing display lock.

## Owner decisions

Resolved by the owner on 2026-10-29.

| ID | Topic | Decision |
|---|---|---|
| `D-1` | Card selection | The product route is a forward transition to `PinEntryViewController` (name provisional). In this slice `NoDestinationViewController` stands in for it: the transition runs, the destination is empty. Login does not advance the session into PIN (`select_pin_user()` is not called), and no sign-in edge fires |
| `D-2` | Mute | Mute latch: display off and sign-out. No user is signed in during this slice, so sign-out is a no-op beyond the session reset. Mute release: refresh the hangout as today and Root presents `LoginViewController`. Login is the return point from mute on every screen |
| `D-3` | BOOT and RED | Each controller declares its own BOOT/RED handling. `LoginViewController` and `NoDestinationViewController` declare none, and the runtime does not read BOOT/RED while either is current or while a transition is busy. The mute latch is always read |
| `D-4` | Inbox, Send, Volume, Record, stage router | Stay compiled and unreachable. Nothing loads or attaches them; guardrails prove no call path from boot, the tick, or any controller. Deletion is a later package |
| `D-5` | Host testing | Real LVGL on host: the pinned `managed_components` LVGL with a headless display driver. No fake LVGL. See P1 |
| `D-6` | Presentation invariants | Amended in this slice. P4 changes PRES-TICK, PRES-OWNER, PRES-SCREEN, PRES-MUTE, and PRES-HIDDEN for Root ownership in the same change that rewrites guardrails |
| `D-7` | Source location | All controllers, the coordinator, and the runtime live in `client/device/main` |

## Requirement traceability

| ID | Required outcome | Status today | Package |
|---|---|---|---|
| `R-VC-001` | Create and unit-test `RootViewController` | Missing | P1 |
| `R-VC-002` | Create and unit-test `NoDestinationViewController` | Missing | P1 |
| `R-VC-003` | Create and unit-test `SplitPaneViewController` | Missing; global `stage_shell::SplitPane` exists | P2 |
| `R-VC-004` | Create and unit-test `LoginViewController : SplitPaneViewController` | Missing; Login is a global bridge | P3 |
| `R-TRANS-001` | Implement the existing forward CoverUp motion without two live controller trees | Labs/spec exist; product helper missing | P1, P4 |
| `R-WIRE-001` | Disconnect feature/layout orchestration from `UiWindow` | Missing | P4 |
| `R-WIRE-002` | Install `RootViewController` under the window content root | Missing | P4 |
| `R-WIRE-003` | Push `LoginViewController` at boot | Missing | P4 |
| `R-NAV-001` | Selecting a roster card transitions forward to the PIN entry position, filled by `NoDestinationViewController` in this slice | Current callback opens PIN | P3, P4 |
| `R-NAV-002` | Mute signs out and blanks; mute release refreshes the hangout and presents `LoginViewController` | Mute blanks and signs out inside the Login bridge | P1, P4 |
| `R-IN-001` | BOOT/RED are declared per controller; Login and empty destination declare none | Buttons route to the global stage router | P1, P4 |
| `R-MEM-001` | Measure the new hierarchy at controller boundaries | Existing probes are phase/feature-oriented | P5 |
| `R-BUILD-001` | `make app:build` succeeds | To verify after cutover | P6 |
| `R-DEVICE-001` | Flash and boot the matching build | To verify on device | P6 |
| `R-AC-001` | Hangout roster and user cards appear | Current path does; replacement must prove it | P6 |
| `R-AC-002` | Selecting a user ends on the empty destination | Missing | P6 |
| `R-RUN-001` | The product tick, mute/backlight, buttons, and Wi-Fi keep running after Login unloads | Tick lives in the Login bridge | P3, P4 |
| `R-TEST-001` | Host harness can compile and run controller code against real LVGL | Host tests cover pure models only | P1 |
| `R-LOG-001` | Serial markers prove controller lifecycle and transition events on device | Missing | P4, P6 |
| `R-DOC-001` | Presentation invariants describe Root ownership | Describes `product_stage::present()` | P4 |

## Repository map

| Path | Present role and delta |
|---|---|
| `client/device/main/product_ui_window.{hpp,cpp}` | Currently mounts the global split pane and orchestrates Login/Record. Reduce to screen and content-root ownership. |
| `client/device/main/product_app_run.cpp` | Currently creates `UiWindow` and calls `mountDesk()`. Change to construct Root and push Login. |
| `client/device/main/product_login_bridge.cpp` | Mixes Login service, widgets, timer, routing, and eager Inbox/Send/Volume attachment. Extract the Login-only service/view boundary. |
| `client/device/main/product_stage_router.cpp` | Current global visibility owner. It must not remain a second owner in the new boot slice. |
| `client/shared/stage_shell/product_split_pane/` | Contains reusable geometry/build evidence but is a global singleton, not the requested controller ownership model. |
| `client/labs/lab-06-stage-shell/` | Contains the ported 400 ms forward/reverse cover transition evidence. |
| `docs/product/device-ui/patterns/TRANSITION-COVER-DOWN.md` | Active transition contract: incoming root `+240 → 0`, 400 ms, ease-in-out quint. |
| `scripts/device_guardrails/` | Encodes the old timer/presenter helper names and must move to ownership invariants. |
| `scripts/tests/test_device_guardrails.py` | Currently expects `mountDesk()`, `mountRecord()`, and old boot markers. Update for Root/Login ownership. |
| `scripts/device_guardrails/presentation_invariants.py`, `presentation_path.py` | Encode PRES-TICK/PRES-OWNER around `on_login_timer` and `product_stage::present()`. Rewrite with the amended invariants. |
| `docs/standards/device-presentation-invariants.md` | Normative tick/visibility/power model. Amend for Root ownership (`D-6`). |
| `client/device/sdkconfig.defaults` | Holds `CONFIG_LV_MEM_SIZE=32768`. Add `CONFIG_LV_USE_SNAPSHOT=y`; pool size unchanged. |
| `client/device/main/CMakeLists.txt` | Register controller and runtime sources. |
| `client/device/main/app_main.cpp` | Owns `boot_backlight_after_display`; unchanged, but its ordering is an acceptance constraint. |
| `client/shared/login_session/` | Host-tested Login/session models. The extracted Login service builds on these, not on LVGL. |
| `config/app-test-suite.yaml` | Host test discovery. New controller tests must be matched by it. |
| New: controller sources | `client/device/main` (`D-7`): base `ViewController`, `RootViewController`, transition coordinator, `SplitPaneViewController`, `NoDestinationViewController`, `LoginViewController`, `LoginService`, and `ProductRuntime`. The host harness compiles these files from `client/device/main` directly. |

## Present/required delta

| Topic | Present state | Required state | Resolution |
|---|---|---|---|
| Window ownership | `UiWindow` knows Desk, Record, split pane, Login attach | Window owns screen and `content_root` only | P4 removes feature/layout methods and dependencies |
| Root presentation | `product_stage::present()` toggles global feature siblings | `RootViewController` owns exactly one live child hierarchy | P1 + P4 |
| Split ownership | Global `stage_shell::splitPane()` | Per-controller split roots owned by `SplitPaneViewController` | P2 |
| Login | Global bridge constructs unrelated views and global timer | Login controller builds only Login; service work survives independently | P3 |
| Roster tap | Calls `select_pin_user()` | Requests Root navigation to empty destination | P3/P4 |
| Transition | Product path lacks the specified stage transition | Reuse defined forward CoverUp motion without two live trees | Static outgoing snapshot + incoming root animation |
| Measurement | Existing eager implementation dominates readings | Start trustworthy readings at Root/Login load/unload | P5 |
| Product tick | `on_login_timer` in the Login bridge runs input, Wi-Fi, session, services, and present | App-level runtime tick that survives Login unload | P3 extracts, P4 installs |

## Runtime ownership after cutover

Today one 50 ms `lv_timer` (`on_login_timer`) inside the Login bridge runs every
phase of the presentation model: hardware buttons and mute latch, Wi-Fi
schedule, Login session and HTTP results, feature services, then
`product_stage::present()`. Unloading the Login controller must not stop it.

- A `ProductRuntime` (name provisional) owns the single `lv_timer` and is
  created in `product_app_run.cpp` after Root. It is not a view controller.
- Phase order stays INPUT → CONNECTIVITY → SESSION → SERVICES → PRESENT, and
  the tick never returns early (PRES-ORDER, PRES-NORETURN).
- INPUT keeps the mute-level reconcile and `blank_login_display` /
  `wake_login_display` behavior on every screen, including the empty
  destination and mid-transition (PRES-POWER, PRES-MUTE). Those functions move
  with the runtime, not with the Login controller.
- INPUT reads BOOT/RED only when Root's current controller declares a handler
  and no transition is busy (`D-3`). In this slice no controller declares one,
  so BOOT/RED are not read.
- Mute flow (`D-2`):
  1. Latch: if a transition is busy, the coordinator finishes it immediately
     (incoming root at `y=0`, `viewDidAppear`, snapshot released). Then
     `blank_login_display` and sign the Login session out.
  2. While latched: the current controller stays as it is; nothing renders
     because the display is off.
  3. Release: refresh the hangout as today. If the current child is not Login,
     Root performs a non-animated replacement to `LoginViewController`. Then
     `wake_login_display`. Login is fully appeared before the backlight is on.
  4. If Login is already current, it is not reloaded; it renders the refreshed
     roster snapshot.
- SESSION advances the extracted `LoginService` whether or not the Login
  controller is loaded.
- SERVICES calls no Inbox, Send, Volume, or Record service in this slice.
- PRESENT asks Root's current child, if any, to render from service snapshots.
  Root is the only visibility owner; `product_stage::present()` is not called.
- `lv_anim` used by the coordinator is LVGL-internal and is not a second
  product `lv_timer`; the amended PRES-TICK says so explicitly.
- The single `lv_indev_reset` stays in `wake_login_display` (PRES-POINTER).
  Controller unload relies on LVGL's delete-time pointer reset.

## P1 — lifecycle, root, empty destination, and transition coordinator

### Outcome

A host-testable controller lifecycle with one root owner, a valid empty root,
the empty destination, and a transition coordinator capable of the existing
forward CoverUp motion.

### Deliverables

- Base `ViewController` lifecycle contract:
  `loadView`, `viewDidLoad`, `viewWillAppear`, `viewDidAppear`,
  `viewWillDisappear`, `viewDidDisappear`, and `unloadView`.
- `RootViewController` with exactly one current child.
- `NoDestinationViewController` with no labels, controls, timers, feature
  services, or slot chrome. `loadView` creates exactly one root: 320×240,
  opaque (`LV_OPA_COVER`), borderless, no padding, not scrollable, with background
  `product_theme::Dark::kSurfaceRgb24`. The root has no children.
- The destination root supplies the opaque moving cover and is the only object
  the coordinator animates. `UiWindow` paints
  `product_theme::Dark::kSurfaceRgb24` on the screen and `content_root` is
  transparent; the window color does not move. Do not add a manager-owned
  full-screen cover: that adds an LVGL allocation at peak and cannot replace
  the destination's required opacity. LAB-06's `transition_cover` is evidence
  for an opaque 320×240 moving object, not another product owner.
- The fixed outgoing snapshot remains behind the incoming root until
  `viewDidAppear`, then its image is deleted. The settled empty screen is the
  same opaque destination root, so deleting the snapshot does not change its
  color or expose a hole.
- Container-driven appearance API and failure contract.
- Transition coordinator whose forward transition traces to
  `TRANSITION-COVER-DOWN` in this order. Callback names below describe effects
  of Root's begin/end appearance brackets, never direct callback calls:
  1. Begin outgoing disappearance: `viewWillDisappear` while its tree is still
     the live child.
  2. Copy the 320×240 frame into the bounded static image and show it. Disable
     input.
  3. Remove the outgoing root. End disappearance: `viewDidDisappear`, then
     `unloadView`. The controller object may survive the callback; its LVGL tree
     is no longer in the display hierarchy. `unloadView` then deletes the
     detached tree and invalidates its handles.
  4. Incoming `loadView` and `viewDidLoad` if needed; begin appearance to emit
     `viewWillAppear`; then parent its root at `y=+240`.
  5. Animate the incoming root to `y=0` over 400 ms, ease-in-out quint.
  6. End incoming appearance to emit `viewDidAppear` after animation completion.
  7. Delete the image and release the snapshot.
- Non-animated replacement (used by mute release): outgoing
  `viewWillDisappear`, remove, `viewDidDisappear`, `unloadView`; then incoming
  load, `viewWillAppear`, parent at `y=0`, `viewDidAppear`. No snapshot and no
  animation, so it cannot fail on snapshot allocation. Incoming load failure
  leaves Root valid with no child, as in the animated path.
- Finish-now: the coordinator can complete a busy forward transition
  immediately for mute, with the same end state and callbacks as natural
  completion.
- Per-controller hardware input: the base `ViewController` exposes an optional
  BOOT/RED handler declaration. The default is none.
- `viewDidLoad` runs once per load. Appearing again while still loaded does not
  repeat it. This slice unloads on departure, so a later visit loads again.
- No interactive cancellation. Do not copy UIKit navigation-controller
  interleaving that requires two live trees.
- Snapshot allocation failure after `viewWillDisappear`, before removal:
  retain the outgoing tree and return the same child to appeared through the
  appearance API (`viewWillAppear`, already parented, `viewDidAppear`). Do not
  emit `viewDidDisappear`, unload it, or empty Root. The base appearance state
  machine must support this interrupted disappearance without a false `did`.
- Incoming load failure after outgoing unload: release the snapshot and any
  partial incoming tree. Root stays valid with no child. This is a failure
  outcome, not an interactive cancel.

### Snapshot capture

The 153,600-byte PSRAM buffer is the redraw destination. Capture synchronously
redraws the outgoing controller root into it and returns before that root is
removed or deleted.

The panel cannot be read back. `CONFIG_BSP_LCD_DRAW_BUF_HEIGHT` is 40 and
double-buffering is off, so the configured driver stripe is 320×40 RGB565
(25,600 bytes), not a 320×240 frame. Do not use a partial flush stripe as a
snapshot source. The actual product runtime draw-buffer height must also be
distinguished from the BSP configuration; neither is a complete frame.

Enable `LV_USE_SNAPSHOT` in the product configuration. Capture uses LVGL's
snapshot redraw into caller-owned memory, never `lv_snapshot_take`: that API
allocates pixels with `lv_malloc`, which would ask the 32 KiB LVGL pool for
153,600 bytes.

The transition coordinator performs these steps on the LVGL thread after
outgoing `viewWillDisappear` and before `viewDidDisappear`:

1. Allocate one PSRAM block with `heap_caps_malloc` and `MALLOC_CAP_SPIRAM`.
   The pixel payload is 320×240 RGB565, 153,600 bytes. Padding required by
   `LV_DRAW_BUF_ALIGN` is included in the same PSRAM block. No payload or
   alignment padding comes from the LVGL pool or internal SRAM. Keep the base
   allocation pointer for release separately from the aligned payload pointer.
2. Point a manager-owned `lv_draw_buf_t` at the payload:
   `LV_COLOR_FORMAT_RGB565`, width 320, height 240, stride 640.
3. Call `lv_snapshot_take_to_draw_buf` on the outgoing controller root. It lays
   out and redraws the root through `lv_obj_redraw` directly into the supplied
   buffer. The call returns only after redraw completes; the outgoing tree
   remains intact throughout.
4. If allocation or redraw fails, free any PSRAM allocation, create no image,
   and leave the outgoing controller loaded. Return that same child to appeared
   through Root's appearance API.
5. On successful redraw, create one `lv_image` parented to the window
   `content_root`, not to the outgoing controller. Its manager-owned
   `lv_image_dsc_t` references the PSRAM payload without copying pixels. The
   image is 320×240, opaque, at `y=0`, behind the incoming root's moving
   position. If image creation fails, release the snapshot and restore the
   still-loaded outgoing child through the same failure path.
6. Only after synchronous redraw returns and the image is installed, remove
   the outgoing root, finish disappearance through the appearance API, and
   call `unloadView`. Outgoing teardown does not free the image, descriptor,
   draw-buffer header, or PSRAM block.

The LVGL pool pays for temporary redraw state and one image object while the
outgoing tree is still alive. It does not pay for a second full frame. A
snapshot-sized intermediate layer is a failed capture, never a fallback into
the 32 KiB pool. Login and the empty destination use ordinary object trees so
redraw targets the supplied buffer.

After incoming `viewDidAppear`, delete the image and free the original PSRAM
allocation, including alignment padding. Keep the image descriptor and draw
buffer header alive for every operation that references them.

### Transition input gate

PRES-HIDDEN makes hiding the only input gate, so the coordinator does not
reorder siblings. While busy: the snapshot image is not clickable; the incoming
root gets `LV_OBJ_FLAG_CLICKABLE` cleared on its root and the coordinator
rejects navigation requests; the runtime does not read BOOT/RED (`D-3`). Mute
is still read and uses finish-now. The
flag is restored before `viewDidAppear`. The amended PRES-HIDDEN records this
as the one sanctioned non-hiding gate.

### Host test harness

Existing host tests compile pure C/C++ models with no LVGL. P1 adds:

- a host build of the pinned LVGL from `client/device/managed_components/lvgl__lvgl`
  with a product-equivalent `lv_conf` (32 KiB pool, `LV_USE_SNAPSHOT`, RGB565)
  and a headless 320×240 display whose flush callback discards pixels;
- a manually advanced tick (`lv_tick_set_cb`) so 400 ms animation tests are
  deterministic;
- an allocation-failure seam for the PSRAM allocator (on host, `heap_caps_malloc`
  is replaced by an injectable function) and a way to exhaust the LVGL pool for
  load-failure tests;
- a recording `ViewController` test double for callback order;
- a Python runner under `scripts/tests/` matched by `config/app-test-suite.yaml`,
  in the same pattern as the existing compiled host tests.

### Unit tests

- initial child installation;
- exact replacement callback/removal/load sequence specified above;
- appearance callbacks are reached only through the container's begin/end API;
- boot appearance has no disappearance callbacks and no cover animation;
- one current child;
- outgoing removal precedes `viewDidDisappear`, which precedes unload; outgoing
  unload completes before incoming load;
- no simultaneous live controller trees;
- snapshot failure restores the same appeared child without unload or
  `viewDidDisappear`;
- incoming-load failure releases partial resources and ends on empty Root;
- `viewDidLoad` occurs once per load, not once per appearance;
- repeated unload safety;
- root with no child;
- `NoDestinationViewController` creates its single root with no children;
- at cover start, the incoming root has fully opaque background equal to the
  window surface color (`product_theme::Dark::kSurfaceRgb24`) and 320×240 bounds;
- transition starts at `y=240`, ends at `y=0`, lasts 400 ms, and uses the
  configured easing path;
- `viewDidAppear` occurs only after the 400 ms animation completes;
- snapshot and animation resources are released on success and failure;
- input is rejected while the transition is busy;
- non-animated replacement emits the full callback order with no snapshot or
  animation, and ends with the incoming root at `y=0`;
- finish-now during the animation yields the same end state and callbacks as
  natural completion, and releases the snapshot;
- a controller with no BOOT/RED declaration receives no button intents.
- pixel payload is 153,600 bytes in PSRAM, with alignment padding in the same
  block; LVGL-pool usage does not rise by the pixel-payload amount;
- capture calls `lv_snapshot_take_to_draw_buf`, never `lv_snapshot_take`;
- the outgoing root exists throughout redraw and at its return; the snapshot
  image is installed after successful return and before outgoing removal or
  `unloadView`;
- the image's parent is `content_root`, and outgoing unload leaves it present;
- allocation or redraw failure frees PSRAM, creates no image, and leaves the
  outgoing controller loaded and returned to appeared;
- image-creation failure releases snapshot resources and restores the loaded
  outgoing child;
- image deletion precedes freeing its backing PSRAM and descriptor lifetime.

### Exclusions

- Interactive transition cancellation
- Animated reverse navigation (mute release uses the non-animated replacement)
- Transition selection by product route
- Sketch surface leasing
- PIN behavior

## P2 — `SplitPaneViewController`

### Outcome

An instance-owned split-pane controller suitable as the base of Login.

### Deliverables

- `SplitPaneViewController : ViewController`.
- Instance-owned root, upper slot, and lower slot.
- Layout from `LAYOUT-SPLIT-PANE`: 320×240 canvas, 80 px upper, 160 px lower.
- Slot backgrounds, clipping, non-scrollable chrome, and pointer invalidation.
- Complete rollback if root or either slot cannot be created.
- No dependence on the global `stage_shell::splitPane()` singleton.

### Unit tests

- exact geometry;
- root/slot parentage;
- construction failure at each allocation step;
- teardown deletes the complete owned subtree;
- handles are null after unload;
- reload creates a valid new hierarchy;
- no feature widgets or services are created.

## P3 — `LoginViewController`

### Outcome

`LoginViewController : SplitPaneViewController` displays the hangout roster and
requests navigation when a valid user card is selected.

### Deliverables

- Extract Login session/HTTP progression from the old widget-attach bundle.
- Preserve the server roster, connecting, and `no Wi-Fi` inside Login;
  connection status remains lower-slot presentation on that controller.
- Login controller renders only:
  - loading/connecting;
  - no-Wi-Fi status required to reach roster safely;
  - empty hangout;
  - sign-in heading;
  - roster cards and carousel interaction.
- No PIN, Inbox, Send, Volume, or Record widget construction.
- Narrow navigation callback/delegate carrying the selected user identity.
- Card selection centers/focuses as applicable, then asks Root to navigate to
  `NoDestinationViewController`.
- The temporary destination overrides the final PIN route only for this slice.
- Login service state survives controller unload; LVGL handles do not.
- Extract `LoginService` from the bridge: session, HTTP worker results, Wi-Fi
  input, and roster snapshots, with no LVGL dependency. It is advanced by the
  runtime tick, not by the controller.
- Move the tick, mute/backlight, and button polling out of the bridge into the
  runtime (see [Runtime ownership after cutover](#runtime-ownership-after-cutover)).
- Remove the bridge's Inbox/Send/Volume attachment and `set_wifi` fan-out to
  Inbox/Send from the Login path.
- Selection applies `D-1`: the navigation request names the PIN entry
  destination and the selected user; Root resolves it to
  `NoDestinationViewController` in this slice. No `select_pin_user()`, no
  sign-in edge.
- `LoginService` exposes sign-out for mute and a hangout refresh for mute
  release, matching today's behavior.
- Render is pure (PRES-RENDER): it reads the service snapshot and writes only
  Login widgets with change-detected setters.
- Carousel cards are reordered only within the collection (PRES-HIDDEN allows
  `lv_obj_move_to_index` there).

### Unit tests

- loading, empty, and populated roster snapshots;
- only Login widgets are constructed;
- valid selection requests one navigation with the correct identity;
- scroll gesture does not navigate;
- invalid/stale card index does not navigate;
- callback cannot fire after unload;
- unload clears every LVGL handle;
- re-entry renders current service state without rebooting/resetting the
  service journey;
- `LoginService` advances roster/connecting/no-Wi-Fi states with no
  controller loaded;
- selection does not change the session to a PIN or signed-in state;
- rendering the same snapshot twice changes no widget.

## P4 — product cutover at `UiWindow`

### Outcome

Product boot creates the new Root/Login hierarchy and no longer enters the old
eager view path.

### Deliverables

- Reduce `UiWindow` to active screen, fixed canvas/background, and
  `content_root`.
- Remove `Scene`, `mountDesk`, `mountRecord`, `deskRoots`, split-pane mounting,
  Login attachment, Record release, and feature knowledge from `UiWindow`.
- Update `product_app_run.cpp`:

```text
create UiWindow
→ create RootViewController(content_root)
→ create LoginViewController(LoginService, navigation delegate)
→ push LoginViewController
→ create ProductRuntime (single lv_timer; guarded backlight re-assert)
→ refresh and probe
→ product_ui_ready
→ start network
```

First installation has no outgoing child and does not use
`TRANSITION-COVER-DOWN`. Root drives this sequence through the appearance API:

```text
loadView → viewDidLoad → viewWillAppear → parent at y=0 → viewDidAppear
```

- Route Login's user-selection callback through Root using the P1 forward
  CoverUp transition to `NoDestinationViewController`.
- Ensure the old `product_stage::present()` and `product_login::attach()` paths
  are not invoked by boot or roster selection.
- Keep existing storage, microphone, display, backlight, NVS, ready-marker, and
  Wi-Fi ordering.
- Update build component registration for the new sources.
- Rewrite guardrails around ownership rather than retaining obsolete helper
  names.
- Add `CONFIG_LV_USE_SNAPSHOT=y` to `client/device/sdkconfig.defaults`.
- Amend `docs/standards/device-presentation-invariants.md` (`D-6`) in the same
  change as the guardrail rewrite, so standard, checks, and code agree.
- Apply `D-4` to legacy feature sources; update or retire host tests that
  assert the old `product_stage::present()` model.
- Emit serial markers (one line each, `APP_LOG_TAG`):

```text
vc_load name=<controller>
vc_unload name=<controller>
vc_transition_begin from=<controller> to=<controller>
vc_snapshot ok=<0|1> region=psram bytes=153600
vc_transition_end to=<controller> ms=<elapsed>
vc_construct name=<feature>   (from any legacy feature attach; must never appear)
```

### Verification

- Source scan: `UiWindow` has no feature/layout dependencies.
- Source scan: Login controller cannot attach Inbox/Send/Volume/Record.
- One presentation timer/clock owner.
- `product_ui_ready` occurs after Root and Login are loaded.
- Wi-Fi starts after `product_ui_ready`.
- No old presenter can change visibility or root containment.
- Exactly one product `lv_timer`, owned by the runtime, not by any controller.
- Mute/backlight functions are reachable from the runtime with Login unloaded.

## P5 — replacement-architecture memory probes

### Outcome

The first measurements describe the new Root/Login slice, not the rejected
eager implementation.

### Probe points

```text
root_created
login_load_before
login_load_after
login_appeared
login_will_disappear
transition_snapshot_before
transition_snapshot_after
login_will_unload
login_unloaded
no_destination_load_before
no_destination_load_after
no_destination_will_appear
transition_complete
```

`login_will_disappear` precedes snapshot capture.
`no_destination_will_appear` precedes the cover animation.
`login_will_unload` remains after `viewDidDisappear`.

Record LVGL total/free/largest/max-used; internal, DMA-capable internal, and
PSRAM free/largest; transition snapshot bytes and region; controller load,
unload, and animation duration.

### Acceptance

- Login fits the unchanged 32 KiB LVGL pool.
- Login unload returns its owned LVGL allocation.
- Transition snapshot allocation has a declared region and bounded lifetime.
- Empty destination loads without allocation failure.
- Repeated host lifecycle tests do not show declining recoverable LVGL space.

No current-screen qualification is required.

## P6 — build, flash, and physical acceptance

### Commands

```text
make test
make app:build
make app:flash
```

Capture serial for the exact flashed build ID.

### Acceptance criteria

1. Build succeeds and publishes a device package.
2. Flash succeeds on the selected device.
3. Serial contains `product_ui_ready build=<flashed-build-id>`.
4. No panic, Guru Meditation, stack overflow, or UI attach failure appears.
5. The hangout roster appears with user cards.
6. A user card can be selected normally.
7. Selection runs the specified forward CoverUp motion: incoming destination
   moves upward from the bottom over 400 ms.
8. The outgoing Login controller is no longer live after its static snapshot is
   established.
9. At transition completion, the screen displays
   `NoDestinationViewController`: one opaque surface-colored 320×240 root with
   no children, labels, controls, timers, feature services, or slot chrome.
   Snapshot removal does not change the settled background color.
10. PIN, Inbox, Send, Volume, and Record widgets were never constructed.
11. Memory logs include the new Root/Login/transition boundaries.
12. Serial shows `vc_unload name=Login` before `vc_load name=NoDestination`,
    `vc_snapshot ok=1 region=psram`, and `vc_transition_end` with `ms` near
    400. No `vc_construct` line appears (evidence for items 8 and 10).
13. On the empty destination, latching mute turns the display off. Releasing
    mute refreshes the hangout and shows `LoginViewController` with the roster
    and touch responsive; serial shows `vc_unload name=NoDestination` before
    `vc_load name=Login`, with no `vc_transition_begin` (`D-2`).
14. Latching mute during the 400 ms transition ends the transition; releasing
    it shows Login.
15. BOOT and RED presses on Login and on the empty destination do nothing
    (`D-3`).
16. Booting with mute latched keeps the display dark through UI attach
    (PRES-POWER), and unlatching shows the roster.
17. Pulling Wi-Fi before boot shows `no Wi-Fi` inside Login, and the roster
    appears after Wi-Fi returns.

## Suggested implementation order

```text
P1 lifecycle/root/empty/transition
→ P2 split pane
→ P3 Login
→ P4 product cutover
→ P5 replacement measurements
→ P6 build/flash/device acceptance
```

P1–P3 are host-testable before changing boot. P4 is the deliberate cutover.
P5 starts measurement only after that cutover. P6 is the first behavior-class
evidence.

## Cross-cutting risks

| Risk | Impact | Mitigation |
|---|---|---|
| Login bridge mixes service, timer, widgets, and unrelated attachments | Roster may stop progressing or eager views may survive | Extract service/view boundaries before cutover; source guardrail forbids unrelated attach |
| Current transition spec was demonstrated with two live layers | Direct reuse would conflict with no-live-overlap decision | Reuse its motion/timing/easing with a bounded outgoing snapshot, then unload outgoing before incoming animation |
| Snapshot may be larger than expected or allocated from the wrong region | Transition can consume internal memory needed by UI/network | Declare dimensions/format/capability and log allocation region/lifetime |
| Global split-pane singleton leaks into new controller | Breaks instance ownership and lifecycle tests | New controller owns its LVGL roots; global singleton is not the target API |
| Guardrails encode old helper names | Correct replacement fails tests or obsolete architecture is preserved | Rewrite checks around ownership/order invariants |
| Moving the tick out of the Login bridge reorders phases | Mute or stale-HTTP regressions already documented in PRES-ORDER | Port phases verbatim; keep `test_presentation_tick_phase_order` passing against the runtime |
| Host LVGL build diverges from device config | Tests pass on host, pool exhaustion differs on device | Generate host `lv_conf` from the same sdkconfig values; P5 device readings are authoritative |
| Temporary selection route differs from final PIN experience | Scaffold could accidentally become product behavior | Mark route as first-slice-only and replace in the PIN implementation package |

## Explicit exclusions

- PIN entry/authentication
- Inbox, settings/Volume, recipients, sketch/record, and Send UI
- Surface-arena implementation
- LVGL pool resizing
- Qualification of current eager screens
- Storage-full behavior
- Two-live-controller transitions
- Animated reverse transitions
- `PinEntryViewController`
- Migration of later controller types

## Independent verifier brief

The verifier should confirm that implementation follows the named controller
types and inheritance, that `UiWindow` no longer owns feature/layout behavior,
that Login cannot eagerly construct unrelated features, and that card selection
uses Root navigation. Host tests must prove lifecycle order and failure cleanup.
Device evidence must prove the roster, defined forward CoverUp motion, empty
destination, matching boot marker, and absence of forbidden crash text.

Compilation alone does not satisfy the physical acceptance criteria.

