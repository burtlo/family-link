# Device UI view-controller architecture — owner-directed revision

Date: 2026-10-29
Revision: 4
Status: **owner-directed architecture** — memory sizes and later implementation
details remain measurement-gated
Audience: product owner and implementers
Scope: `client/device/` product UI runtime; this document does not authorize
implementation or change current normative standards

## Owner direction captured by this revision

This revision replaces the earlier shell/host-centered proposal with a
view-controller architecture.

The requested direction is:

- The stable top-level UI owner is `RootViewController`, not
  `RootShellController`.
- `RootViewController` is not synonymous with a split-pane interface. It can
  install a `SplitPaneController`, a `SketchViewController`, or another future
  full-canvas child.
- `SplitPaneController` is a reusable container view controller. It owns the
  split layout and contains child view controllers; it is not “the desktop
  app,” and it is not named `SplitDeskHost`.
- Login, PIN entry, inbox, recipient picker, sketch, and settings are view
  controllers with explicit view lifecycles.
- View-controller lifecycles own and release LVGL object trees. Long-lived
  sessions, workers, models, and device services do not live in those trees.
- `RootViewController` coordinates transitions and memory admission for the
  currently displayed controller and the controller about to be displayed.
- Large working buffers should come from an explicitly owned shared arena or
  lease broker where their formats and lifetimes are compatible. That arena is
  separate from LVGL's object allocator.
- Names and structure should follow familiar iOS view-controller concepts
  where those concepts clarify containment, appearance, and ownership. The
  design does not attempt to reproduce UIKit APIs mechanically.

These points supersede the previous document's `RootShellController`,
`SplitDeskHost`, `SketchHost`, “desk scene,” and “host swap” language.

## Purpose

The architecture should make four things easy to reason about:

1. Which controller owns every live LVGL object.
2. Which controller is visible and able to receive input.
3. What must survive when a view disappears.
4. Whether the outgoing and incoming views fit during a transition.

The root is therefore a composition, transition, and resource-admission owner.
It is not a feature service, a screen-specific layout, or a collection of
permanent hidden views.

## Proposed runtime composition

```text
product_app::run
├─ AppServices                         boot/session lifetime
│  ├─ AuthenticationService
│  ├─ InboxService
│  ├─ PlaybackService
│  ├─ RecipientService
│  ├─ RecordService
│  ├─ SendService / OutboxService
│  ├─ SettingsService
│  └─ PresentationClock
├─ DisplayResourceCoordinator         UI resource ledgers + leases
└─ UiWindow                            LVGL screen + content root only
   └─ RootViewController               stable root and transition owner
      └─ active child view controller  exactly one active branch
         ├─ SplitPaneController        reusable container when needed
         │  ├─ primary child VC        one scene or panel controller
         │  └─ secondary child VC      one scene or panel controller
         ├─ SketchViewController       direct full-canvas child
         └─ future full-canvas VC      direct child; no split assumption
```

“Exactly one active branch” means one controller hierarchy owns presentation
at steady state. A transition may briefly retain both outgoing and incoming
hierarchies only when its declared memory policy permits the overlap.

## Core types and responsibilities

### `UiWindow`

`UiWindow` owns only the LVGL screen and a canvas-sized `content_root` into
which the root controller installs its child hierarchy. It may apply stable
window-level background and display bounds.

It does not own split-pane geometry, product routing, feature widget trees,
feature workers, or transition-specific memory decisions.

### `RootViewController`

`RootViewController` is created once for the UI runtime. It owns:

- the current child controller hierarchy;
- child containment and removal;
- appearance-transition ordering;
- the single steady-state input owner;
- transition selection: replacement, cover, wipe, or another approved form;
- resource admission for current, outgoing, and incoming views;
- failure handling when the destination view cannot be admitted or loaded;
- coordination with the one presentation clock.

It receives route intent and current model state. It does not perform HTTP,
own authentication state, manage an upload worker, or retain feature data that
must survive a view's destruction.

The root API should describe controller containment rather than layout hosts.
An illustrative shape is:

```cpp
class RootViewController final : public ViewController {
public:
    TransitionResult setViewController(
        std::unique_ptr<ViewController> next,
        const Transition& transition);

    ViewController* presentedViewController() const;
};
```

This is a responsibility sketch, not an approved C++ interface.

### `SplitPaneController`

`SplitPaneController` is a container view controller for
`LAYOUT-SPLIT-PANE`. It owns:

- the split-pane LVGL container and slot geometry;
- installation and removal of its child view controllers;
- clipping, backgrounds, and slot bounds;
- forwarding appearance transitions to its children in deterministic order;
- the combined resource declaration for its own chrome plus both children.

It does not know login, inbox, settings, or “desktop” routing policy. A route
chooses to present a configured `SplitPaneController`; the controller itself
does not decide which product journey comes next.

Its slots contain view controllers, not unowned scene fragments. A child owns
all LVGL objects it creates inside the slot root passed to it.

### Leaf view controllers

| Controller | Presentation role | Typical parent |
|---|---|---|
| `LoginViewController` | Roster, connecting, and signed-out login presentation | `SplitPaneController` or a login-specific container composition |
| `PinEntryViewController` | PIN status and keypad | `SplitPaneController` |
| `InboxViewController` | Message carousel and playback presentation | `SplitPaneController` |
| `RecipientPickerViewController` | Target/recipient selection | `SplitPaneController` |
| `SketchViewController` | Recording, sketch canvas, and palette composition | Direct child of `RootViewController` unless a later accepted composition introduces a dedicated container |
| `SettingsViewController` | Settings presentation, currently Volume | `SplitPaneController` |
| `SendFeedbackViewController` | Sending, sent, and failure feedback | `SplitPaneController` |

The table names controller responsibilities; it does not collapse existing
`SCR-*` compositions or experience stages. If one scene needs multiple child
controllers, that composition should be explicit rather than hidden inside a
generic “host.”

## View-controller containment contract

Every view controller has one parent or is the root. Every live LVGL object has
one owning view controller.

A container performs child installation in this order:

1. establish the parent-child relationship;
2. provide a parent LVGL object and bounded geometry;
3. load the child's view;
4. complete containment;
5. begin appearance only when the hierarchy is ready to present.

Removal reverses the relationship only after the child's disappearance and
view teardown are complete. A child must not retain its former slot root after
removal.

No feature module may attach widgets directly to `UiWindow`, the root content
object, or another controller's subtree. `SketchViewController` receives the
root content bounds because it is installed as the root's direct child; that
does not make it a special global owner.

## View lifecycle

The lifecycle separates controller lifetime, view lifetime, and appearance.
A controller may exist without a loaded LVGL tree, but its feature state must
reside in a model or service if it must survive `unloadView()`.

| Lifecycle function | Responsibility |
|---|---|
| `loadView(parent)` | Create this controller's root LVGL object and owned subtree under `parent`; bind initial model snapshots; acquire admitted view resources |
| `viewDidLoad()` | Complete one-time setup for this loaded tree; no service boot or journey reset |
| `viewWillAppear(context)` | Refresh from model state, prepare animations, and remain input-disabled until the transition establishes ownership |
| `viewDidAppear(context)` | Become the active input owner; start view-scoped motion or timers |
| `viewWillDisappear(context)` | Stop accepting new input; stop view-scoped activity; persist transient state that the experience requires to survive |
| `viewDidDisappear(context)` | Finish appearance work; no assumption that the controller or its view is already destroyed |
| `unloadView()` | Delete the owned LVGL subtree, null every LVGL pointer, return view leases, and leave the controller safe to load again |
| controller destructor | Release controller-only resources; never serve as the first opportunity to quiesce a live worker |

Required lifecycle properties:

- Calls run on the LVGL task.
- Repeated appearance calls are deterministic and do not reset a journey.
- `loadView()` either succeeds completely or rolls back its partial tree and
  leases.
- `unloadView()` is idempotent after a successful teardown.
- No LVGL pointer is stored in a service, session model, worker job, or another
  controller.
- A disappearing view cannot receive input after the transition input gate
  moves away from it.
- View-scoped callbacks use lifetime-safe handles and cannot call back into an
  unloaded controller.

The exact method names may be adapted to the codebase, but implementations
should preserve these semantic boundaries and keep the iOS-style names unless
there is a concrete conflict.

## Transition transaction

`RootViewController` performs a route change as a transaction:

```text
route intent
  → construct destination controller without LVGL objects
  → collect outgoing + incoming resource declarations
  → ask DisplayResourceCoordinator for a transition plan
  → outgoing.viewWillDisappear
  → incoming containment + loadView
  → incoming.viewWillAppear
  → run approved transition with one input owner
  → outgoing.viewDidDisappear
  → remove outgoing containment + outgoing.unloadView
  → incoming.viewDidAppear
  → commit route
```

The resource plan may change the ordering:

- **Replace before build:** unload outgoing first, then load incoming. Lowest
  peak memory; appropriate when no outgoing pixels need to remain visible.
- **Specified snapshot transition:** capture the outgoing presentation into a
  bounded static artifact, unload its controller tree, then load and animate the
  incoming controller according to the applicable existing transition
  specification. The snapshot format, bytes, capability, and lifetime are
  declared.

Owner direction is that no product transition requires two live controller
trees. Do not introduce a generic live-overlap mode. The first implementation
uses the existing forward CoverUp motion identified by
`TRANSITION-COVER-DOWN`: incoming `y=+240 → 0`, 400 ms, ease-in-out quint.

If admission or `loadView()` fails, the root must keep or restore a valid
controller hierarchy and report a route failure. It must not commit a stage
whose view was not installed.

## Display resource coordination

The earlier proposal deferred the main memory question. This architecture
makes it a first-class root dependency.

The detailed resource model, controller envelope worksheet, storage-backed
recording pipeline, shared surface contract, and measurement program are in
[`device-ui-view-controller-memory-envelope.md`](device-ui-view-controller-memory-envelope.md).

Owner direction for this architecture is that inbox playback remains inside
the selected message card. It uses one bounded live playback surface rather
than one uncompressed sketch framebuffer per card. Paused/completed state is
preserved as stroke/playhead state plus an optional bounded storage checkpoint;
it is reconstructed into the shared surface when needed.

### Separate memory ledgers

| Ledger | Current known fact | Coordination model |
|---|---|---|
| LVGL object memory | One global 32 KiB `CONFIG_LV_MEM_SIZE` pool for LVGL-managed allocations | Controllers declare measured steady and peak needs; coordinator admits transitions, but LVGL remains the allocator |
| Shared pixel working memory | Sketch canvas is currently 240×240 RGB565 = 115,200 B (112.5 KiB), allocated outside the LVGL object pool | Explicit exclusive or partitioned leases from a backing arena when consumers are compatible |
| Internal/DMA-capable heap | Needed by display/audio/platform work; overlaps general internal heap capabilities | Capability-aware reservations and measured largest-block floors; not interchangeable with PSRAM |
| PSRAM | Larger but reported unsuitable for some interactive paths without qualification | Allowed only by a controller/resource policy backed by an interaction measurement |
| Transition artifacts | Optional snapshot, wipe, or staging memory | Short-lived lease included in peak admission |

The root can coordinate these ledgers, but it cannot merge them. Deleting LVGL
objects returns bytes to the LVGL pool; it does not enlarge the sketch arena.
Returning a sketch lease does not enlarge `CONFIG_LV_MEM_SIZE`.

### Resource declarations

Each controller provides a resource declaration before `loadView()`:

```cpp
struct ViewResourceRequirements {
    size_t lvglSteadyBytes;
    size_t lvglLoadPeakBytes;
    std::span<const BufferLeaseRequest> bufferLeases;
    size_t transitionArtifactBytes;
    ResourceFallbackPolicy fallback;
};
```

Initial values are estimates until measured on the target. They are admission
inputs, not proof. Container controllers combine their chrome and children's
requirements without double-counting shared leases.

The coordinator should log controller identity, requested bytes/capabilities,
chosen region, measured free/largest blocks, lease owner, and release point.

### Shared working-memory arena

The desired “larger pool” should be modeled as a backing arena for compatible
large working buffers, owned by `DisplayResourceCoordinator` rather than by
`SketchViewController`.

The arena may support:

- one exclusive full-size canvas lease;
- bounded partitions for smaller decoded or playback surfaces;
- a transition lease when a wipe or snapshot needs pixels;
- reconstructing a view from durable or compact model data after reacquisition.

In the intended steady-state modes:

```text
SketchViewController loaded
└─ one 240×240 RGB565 interactive lease

InboxViewController playing in the selected card
├─ one 120×120 RGB565 live playback lease
└─ at most one bounded temporary reconstruction surface if direct rendering
   cannot meet correctness and frame-time requirements
```

Inbox cards without the active lease use their default portrait/name/duration
presentation. Message count does not multiply uncompressed sketch buffers.

A lease transfer requires the old owner to:

1. stop producing or consuming the buffer;
2. quiesce DMA, decoder, draw, and worker references;
3. preserve any state required by the experience in independent storage;
4. invalidate all borrowed pointers;
5. return the lease before the new owner receives it.

The arena size, heap capability, and partition strategy remain open. Initial
allocation is lazy, matching current behavior. Preallocation may be revisited
after the lease coordinator exists and target evidence can compare guaranteed
availability against startup/Wi-Fi/audio pressure. Moving interactive work to
PSRAM has not been qualified.

### LVGL pool strategy

The root should manage LVGL use through lifecycle and admission first:

- keep only the active controller hierarchy loaded at steady state;
- destroy outgoing LVGL trees when transitions no longer need them;
- measure each controller's load peak and steady usage;
- choose replace-before-build when two trees do not fit;
- reject or degrade a transition rather than exhausting the pool.

Increasing `CONFIG_LV_MEM_SIZE` or replacing LVGL's allocator is a separate
engineering decision requiring target measurements. It is not implied by this
architecture.

## Services, models, and controllers

Long-lived services are created outside the view-controller hierarchy and
injected into controllers through narrow interfaces.

| Lifetime | Owns | Must not own |
|---|---|---|
| Application/service | authentication, roster/inbox data, playback position, recipient selection as required, recording state, outbox/upload jobs, settings, worker queues | LVGL objects or callbacks that require a loaded view |
| Controller | scene coordination, formatting, view-local selection/focus where disposable, subscriptions valid for controller lifetime | network task ownership merely because a screen is visible |
| Loaded view | LVGL objects, event bindings, view animations, view-only timers, buffer leases used by that rendering | durable journey state or boot-lifetime workers |

Service initialization and view loading are separate operations. In
particular, a send upload may outlive `SendFeedbackViewController`, and loading
a send view must not reset the send journey. Authentication and inbox workers
must not be destroyed merely because a view is transitioning.

The presentation clock may be a long-lived service, but the root owns the
single presentation pass and lifecycle changes. Leaf views do not create
independent product presentation timers.

### Storage-backed media lifetime

Audio and sketch streams are service/storage resources, not view resources.
Recording uses bounded capture chunks, appends audio and compact sketch strokes
to the local message object, and may stream acknowledged chunks concurrently.
Network failure grows durable backlog, subject to a product storage quota; it
does not grow an unbounded RAM queue or require a complete audio recording in
memory.

`SketchViewController` therefore owns only its live LVGL tree and borrowed
render surface. On disappearance it flushes required stroke state, detaches the
surface from LVGL, returns the lease, and may be destroyed while storage and
upload services continue according to their contracts.

## Routing and controller construction

A router maps product state to a controller description, not directly to
LVGL visibility calls.

```text
ProductRoute.login
  → SplitPaneController(LoginHeaderViewController,
                        LoginViewController)

ProductRoute.pinEntry(person)
  → SplitPaneController(PinStatusViewController,
                        PinEntryViewController)

ProductRoute.inbox
  → SplitPaneController(InboxHeaderViewController,
                        InboxViewController)

ProductRoute.recipientPicker
  → SplitPaneController(RecipientHeaderViewController,
                        RecipientPickerViewController)

ProductRoute.sketch
  → SketchViewController

ProductRoute.settings
  → SplitPaneController(SettingsHeaderViewController,
                        SettingsViewController)
```

This illustrates containment and keeps the split pane reusable. Exact header
controller boundaries may be simplified where one scene controller should own
both slots; that is an implementation choice only if ownership remains
unambiguous and the split controller remains feature-neutral.

Same-route model changes update the current controller without a lifecycle
transition when the visible composition does not change. A route change is the
only path that changes root containment.

## Boot architecture

```text
app_main: existing hardware and storage order
product_app::run:
  create AppServices
  create DisplayResourceCoordinator
  create UiWindow
  create RootViewController(content_root, coordinator)
  construct initial route controller
  admit and install its view hierarchy
  create the single presentation clock
  log product_ui_ready
start network according to BOOT-ORDER
```

Only the initial route's views are loaded. Inbox, recipient picker, sketch,
settings, and send-feedback LVGL trees are not boot prerequisites.

Adopting this meaning requires an explicit rewrite of `BOOT-MARK` and related
guardrails. Until then, the current standard remains authoritative.

## Relationship to current presentation invariants

| Current invariant | Proposed architectural expression |
|---|---|
| `PRES-TICK` | One presentation clock; leaf controllers do not add product timers |
| `PRES-ORDER` | Root presentation pass orders input, services, routing, and view update |
| `PRES-OWNER` | `RootViewController` is the only root-containment and appearance owner |
| `PRES-SCREEN` | One active controller hierarchy at steady state |
| `PRES-HIDDEN` | Hidden remains valid inside a live hierarchy; an inactive scene is normally unloaded, not a permanent hidden sibling |
| `PRES-POINTER` | Lifecycle never performs periodic pointer resets; LVGL object deletion handles deleted active objects |
| `PRES-RENDER` | View updates render snapshots and do not route or mutate another controller |
| `PRES-EDGE` | Session-edge effects remain in services/routing, not view rendering |
| `PRES-LEVEL2` | Child replacement uses container lifecycle; outgoing children are hidden or deleted before incoming input activates |

`PRES-HIDDEN`, `PRES-OWNER`, and `PRES-LEVEL2` need normative wording changes
before implementation. The hardware desk matrix remains required because this
architecture must not reintroduce stacked touch targets, dead PIN input, or
mute/unmute failures.

## Failure and recovery rules

- A controller that cannot obtain required resources does not become current.
- Partial `loadView()` work is torn down before control returns to the root.
- The root retains the outgoing view when possible or presents an explicitly
  designed low-memory failure view whose own budget is reserved.
- A service failure updates model state; it does not destroy the controller
  hierarchy unless routing policy selects a different scene.
- Worker cancellation follows the worker's quiescence contract and is never
  inferred from `unloadView()`.
- Route, controller, transition, resource request, allocation region, and
  failure reason are logged without retaining view pointers.

## As-built contrast

Current firmware differs materially:

- `product_login::attach` constructs several feature trees at boot.
- `product_stage::present` primarily changes visibility among long-lived
  siblings.
- record/sketch releases split content and return paths can rebuild multiple
  modules.
- feature `attach` and `release` operations mix service and widget lifetimes.
- the sketch canvas allocation is retained after the sketch LVGL surface is
  released.
- `UiWindow` currently participates in split layout and record/split switching.

These are migration facts, not requirements for the target design.

## Proposed migration boundaries

This is an architecture sequence, not implementation authorization.

1. Define the base view-controller containment and lifecycle contract with a
   synthetic controller test; do not move feature behavior yet.
2. Separate application services and the presentation clock from feature view
   attach/release functions.
3. Introduce `RootViewController` as the only owner of the `UiWindow` content
   child while preserving current visible behavior.
4. Introduce feature-neutral `SplitPaneController` and move split geometry out
   of `UiWindow` and feature bridges.
5. Migrate one low-risk scene to a leaf controller; verify load, unload,
   re-entry, pointer invalidation, and LVGL pool recovery.
6. Migrate login/PIN and rewrite affected presentation invariants only with the
   desk matrix ready.
7. Migrate inbox, recipient picker, settings, and send feedback while keeping
   their services alive independently.
8. Add `DisplayResourceCoordinator` admission and instrumentation before
   migrating sketch.
9. Define and qualify the shared working-memory arena, then migrate
   `SketchViewController` as a direct root child.
10. Remove the old eager-attach, hide-all, split rebuild, and retained canvas
    paths only after equivalent routes are verified.

Each boundary should leave one authoritative presentation mechanism. Avoid a
long-lived state in which both the old stage presenter and root controller can
change visibility or containment.

## Verification required before implementation can be called complete

- Containment tests prove one parent per controller and one owner per LVGL
  subtree.
- Lifecycle tests cover load failure rollback, repeated entry, repeated exit,
  idempotent unload, and callbacks after unload.
- Resource tests prove admission math for replace-before-build, overlap, and
  snapshot transitions.
- Target instrumentation records LVGL pool free/fragmentation around every
  scene transition and capability-specific heap/largest-block data around
  every large lease.
- Worker tests prove view teardown does not cancel permitted authentication,
  inbox, playback, or upload work and that actual service shutdown quiesces
  workers before destroying queues.
- The device presentation desk matrix passes with saved serial evidence.
- Sketch, playback, touch, and transition latency are measured on the target;
  a host test cannot qualify PSRAM or interaction performance.

## Open architecture decisions

| ID | Decision | Why it matters |
|---|---|---|
| `ARCH-OPEN-001` | Exact shared working-arena size and heap capabilities | Determines whether sketch, playback, and transition artifacts can share backing memory without harming Wi-Fi/audio/display margins |
| `ARCH-OPEN-002` | Whether later evidence justifies changing the accepted initial lazy allocation | Trades fragmentation predictability/availability against early reservation of scarce internal memory |
| `ARCH-OPEN-003` | Per-route measured LVGL steady/load/transition budgets | Required for admission; current estimates are insufficient |
| `ARCH-OPEN-004` | Per-transition static-snapshot envelope or replace-before-build rule | Live controller overlap is not required; specified motion and snapshot peak still need implementation budgets |
| `ARCH-OPEN-005` | Compact/durable representation used to reconstruct sketch and partial playback pixels after a lease transfer | Required before one working arena can safely serve multiple scenes |
| `ARCH-OPEN-006` | Whether login and PIN use separate whole-scene controllers or one parent scene controller replacing lower-slot child controllers | Affects containment granularity but not the root architecture |
| `ARCH-OPEN-007` | Reserved low-memory recovery presentation and budget | Needed to recover deterministically from admission/load failure |
| `ARCH-OPEN-008` | Concrete controller factory and dependency-injection boundary | Must prevent controllers from acquiring global services or constructing workers implicitly |

Memory-specific sizing, storage, surface, inbox-renderer, and allocator-placement
decisions are tracked as `MEM-OPEN-*` in
[`device-ui-view-controller-memory-envelope.md`](device-ui-view-controller-memory-envelope.md)
rather than duplicated here.

## Explicit non-goals

- Treating `SplitPaneController` as the application root or “desktop app”
- Reintroducing `RootShellController`, `SplitDeskHost`, or `SketchHost`
- Keeping every scene's LVGL tree alive and hidden for the boot lifetime
- Retaining two live controller trees for a transition
- Combining the LVGL object pool and sketch pixel memory into one fictional
  interchangeable pool
- Letting view lifecycle methods initialize or reset long-lived services
- Encoding experience behavior, retry policy, copy, or timeouts in this note
- Raising memory settings without target measurements
- One permanent uncompressed sketch framebuffer per inbox card
- Authorizing firmware implementation through this proposal alone

The authorized first implementation slice is mapped in
[`device-ui-view-controller-first-slice-implementation-map.md`](device-ui-view-controller-first-slice-implementation-map.md).

## Review readiness

This revision is ready for owner review of the architecture and terminology.
It is not ready for implementation because `ARCH-OPEN-001` through
`ARCH-OPEN-008`, the affected invariant revisions, measured controller
budgets, and transition failure presentation are unresolved.

The owner-directed root/container/lifecycle structure is no longer presented
as an open naming choice. Silence on the remaining open items is not approval.

## Related evidence and specifications

- [`ui-memory-lifecycle-review-2026-10-25.md`](ui-memory-lifecycle-review-2026-10-25.md)
- [`LAYOUT-SPLIT-PANE.md`](../../product/device-ui/layouts/LAYOUT-SPLIT-PANE.md)
- [`LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md`](../../product/device-ui/layouts/LAYOUT-SPLIT-PANE-FOCUSED-SIDE.md)
- [`FOUNDATION-LVGL-BUDGET.md`](../../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md)
- [`device-target-invariants.md`](../../standards/device-target-invariants.md)
- [`device-presentation-invariants.md`](../../standards/device-presentation-invariants.md)

## Revision history

| Revision | Date | Change |
|---|---|---|
| 1 | 2026-10-29 | Explored a shell/host architecture centered on `RootShellController` and `SplitDeskHost` |
| 2 | 2026-10-29 | Replaced host-centered structure with owner-directed `RootViewController`, `SplitPaneController`, scene view controllers, explicit lifecycle, service separation, and root-coordinated memory admission/leases |
| 3 | 2026-10-29 | Added storage-backed recording/sketch lifetimes, bounded in-card playback surfaces, and the linked controller memory-envelope specification |
| 4 | 2026-10-29 | Recorded lazy initial surface allocation, no live-controller transition overlap, empty-root recovery, replacement-only measurement, and the first implementation map |
