# Device UI view-controller first slice — implementation map

Date: 2026-10-29  
Status: **Ready**  
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

- `RootViewController` is valid with no child and displays nothing.
- `NoDestinationViewController` is the temporary first destination and
  displays nothing.
- `SplitPaneViewController` is a view-controller base/container, not a global
  desktop host.
- `LoginViewController` extends `SplitPaneViewController`.
- Selecting a roster card navigates to `NoDestinationViewController`; PIN is
  deliberately outside this slice.
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
| `R-NAV-001` | Selecting a roster card transitions to `NoDestinationViewController` | Current callback opens PIN | P3, P4 |
| `R-MEM-001` | Measure the new hierarchy at controller boundaries | Existing probes are phase/feature-oriented | P5 |
| `R-BUILD-001` | `make app:build` succeeds | To verify after cutover | P6 |
| `R-DEVICE-001` | Flash and boot the matching build | To verify on device | P6 |
| `R-AC-001` | Hangout roster and user cards appear | Current path does; replacement must prove it | P6 |
| `R-AC-002` | Selecting a user ends on the empty destination | Missing | P6 |

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
  services, or visible content.
- Navigation result/rollback contract.
- Transition coordinator whose forward transition traces to
  `TRANSITION-COVER-DOWN`:
  - input disabled during transition;
  - outgoing presentation captured to a bounded static image;
  - outgoing controller receives disappearance and unloads;
  - incoming controller loads at `y=+240`;
  - incoming root animates to `y=0` over 400 ms;
  - ease-in-out quint path;
  - snapshot is deleted after completion;
  - incoming receives `viewDidAppear` only after completion.
- Failure path: snapshot or incoming-load failure leaves a valid empty Root and
  releases partial resources.

### Unit tests

- initial child installation;
- callback ordering;
- one current child;
- outgoing unload before incoming becomes active;
- no simultaneous live controller trees;
- load failure and rollback;
- repeated unload safety;
- root with no child;
- `NoDestinationViewController` creates no visible content;
- transition starts at `y=240`, ends at `y=0`, lasts 400 ms, and uses the
  configured easing path;
- snapshot and animation are released on success and cancellation;
- input is rejected while the transition is busy.

### Exclusions

- Reverse navigation
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

### Unit tests

- loading, empty, and populated roster snapshots;
- only Login widgets are constructed;
- valid selection requests one navigation with the correct identity;
- scroll gesture does not navigate;
- invalid/stale card index does not navigate;
- callback cannot fire after unload;
- unload clears every LVGL handle;
- re-entry renders current service state without rebooting/resetting the
  service journey.

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
→ refresh and probe
→ product_ui_ready
→ start network
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

### Verification

- Source scan: `UiWindow` has no feature/layout dependencies.
- Source scan: Login controller cannot attach Inbox/Send/Volume/Record.
- One presentation timer/clock owner.
- `product_ui_ready` occurs after Root and Login are loaded.
- Wi-Fi starts after `product_ui_ready`.
- No old presenter can change visibility or root containment.

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
transition_snapshot_before
transition_snapshot_after
login_will_unload
login_unloaded
no_destination_load_before
no_destination_load_after
transition_complete
```

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
   `NoDestinationViewController`, which has no visible content.
10. PIN, Inbox, Send, Volume, and Record widgets were never constructed.
11. Memory logs include the new Root/Login/transition boundaries.

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
| Temporary selection route differs from final PIN experience | Scaffold could accidentally become product behavior | Mark route as first-slice-only and replace in the PIN implementation package |

## Explicit exclusions

- PIN entry/authentication
- Inbox, settings/Volume, recipients, sketch/record, and Send UI
- Surface-arena implementation
- LVGL pool resizing
- Qualification of current eager screens
- Storage-full behavior
- Two-live-controller transitions
- Reverse transitions
- Migration of later controller types

## Independent verifier brief

The verifier should confirm that implementation follows the named controller
types and inheritance, that `UiWindow` no longer owns feature/layout behavior,
that Login cannot eagerly construct unrelated features, and that card selection
uses Root navigation. Host tests must prove lifecycle order and failure cleanup.
Device evidence must prove the roster, defined forward CoverUp motion, empty
destination, matching boot marker, and absence of forbidden crash text.

Compilation alone does not satisfy the physical acceptance criteria.

