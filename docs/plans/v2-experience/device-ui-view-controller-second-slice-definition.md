# View-controller second slice — signed-in navigation

Date: 2026-10-30
Status: Scope defined; package implementation details require repository review before execution.

## Outcome and owner decisions

Extend the completed Root/Login slice with PIN entry, Inbox browsing, settings
(the existing Volume experience), and message-target selection.

Full Inbox playback is deferred by the owner. Valid message-target selection
advances to `NoDestinationViewController` as the temporary recording destination.
Selecting a recipient card toggles membership; advancing requires RED with at
least one valid selected recipient. A card tap alone does not advance.

```text
Login → PinEntry → Inbox
                   ├─ BOOT → Settings (Volume) → BOOT → Inbox
                   └─ RED → MessageTarget
                              ├─ BOOT → Inbox
                              └─ valid selection + RED → NoDestination
```

Mute signs out and blanks the display. Unmute normally returns to Login;
the existing PIN cooldown exception remains: an unexpired cooldown is shown
until its original deadline. Mute does not reset or pause that deadline.

## Governing sources

- [First slice map](device-ui-view-controller-first-slice-implementation-map.md):
  completed containment, appearance, capture, transitions, runtime and probes.
- [Controller architecture](device-ui-shell-architecture-proposal.md).
- [Memory envelope](device-ui-view-controller-memory-envelope.md).
- [Endpoint login](../../product/experiences/endpoint-login.md): PIN, retry,
  cooldown, session refresh, mute, and stale-result rules.
- [Inbox](../../product/experiences/message-carousel-and-playback.md): browsing,
  message identity, selection, refresh, connectivity, settings and sending exits.
  Playback requirements are deferred for this slice.
- [Settings](../../product/experiences/settings.md): Volume only, including
  persistence and hardware-button behavior.
- [Message recording and sending](../../product/experiences/message-recording-and-sending.md):
  recipient selection only; the recording route is temporarily substituted.
- Corresponding `SCR-PIN-ENTRY`, `SCR-PIN-COOLDOWN`, `SCR-MESSAGE-INBOX`,
  `SCR-VOLUME`, and `SCR-RECIPIENT-PICKER` device UI specifications.

Use current specification values when implementing; do not copy historical
timeouts or retry schedules from prior planning text.

## Existing implementation boundary

Current `ProductRuntime` accepts `LoginDestination::PinEntry` and replaces Login
with `NoDestinationViewController`. `serviceFeatures()` is empty. The new slice
replaces that destination and adds signed-in service progression.

`LoginService` already owns the session and HTTP progression independently of
LVGL. Extend its narrow intent/snapshot interfaces for PIN and authentication;
do not expose mutable session ownership to controllers.

The legacy Inbox, Volume, and Send bridges are implementation evidence and
possible sources of reusable model/worker logic. Their eager view attachment,
global presentation routing, and service/view coupling are not the target.

## Controller and service ownership

| Controller | Base | Service-owned state |
|---|---|---|
| `PinEntryViewController` | `SplitPaneViewController` | Selected identity, digits, operation identity, attempts, timeout and verification state in Login/authentication service |
| `PinCooldownViewController` | `ViewController` | Original cooldown deadline in Login/authentication service |
| `InboxViewController` | `SplitPaneViewController` | Authenticated inbox, selected message ID, ordering and refresh state in Inbox service |
| `SettingsViewController` | `SplitPaneViewController` | Endpoint Volume stop and persistence state in Settings service |
| `MessageTargetViewController` | `SplitPaneViewController` | Available recipients, selected ID set and focus in message-target service |

Cooldown is required support for the PIN experience, rather than an optional
extension. Controllers own their LVGL trees; services retain the state needed
to rebuild after navigation. Root remains the exclusive appearance initiator.

## Packages and verification

### S2-P1 — PIN and authentication

Implement the PIN controller, cooldown presentation, and service intents and
snapshots. Carry the selected identity from Login through Root. Enter Inbox
only on server-confirmed authentication; obtain inbox content separately.

Preserve masked entry, checking/disabled keypad, wrong-PIN feedback, conditional
BOOT behavior, partial-entry timeout, transport retry identity, unknown-user
return, lower-slot connectivity states, and full-canvas cooldown. Credential
refresh and best-effort invalidation are service operations independent of view
visibility. Update recency only after authentication succeeds.

Tests: controller load/unload/reload and geometry; correct/wrong PIN; repeated
wrong PIN then success; transport exhaustion and recovery; cooldown across
connectivity loss and mute; BOOT with zero/partial/submitted digits; partial-input
timeout pause/resume; late success after mute; no credentials in logs. Use real
LVGL for controller tests and existing session/worker tests for wire behavior.

Verifier proves one authentication edge, no stale result signing in, and no
retained Login/keypad trees after navigation.

### S2-P2 — Inbox browsing

Implement `InboxService` and `InboxViewController`. Fetch using the current
credential, load empty/populated states, apply last-viewed selection, reconcile
messages by stable ID, and preserve the selected identity across refresh and
controller re-entry. Bound live card widgets to the visible carousel window;
inbox size must not multiply live trees.

Playback is deferred: no audio decoder, playback worker, sketch framebuffer,
per-card sketch allocation, or playable-media download is required. Do not show
a working-looking Play action that silently does nothing. The temporary header
action presentation (omitted or visibly unavailable) is an explicit visual
review item before implementation, not a change to the final experience.

Tests: empty, one, six, and larger-than-visible inbox; first/last card browsing;
stable selected identity on insert/delete/reorder; last-viewed handling; no
autoselection jumps from later snapshots; connectivity recovery; unload/re-entry;
no sketch buffers or media fetches. Verify no old Inbox attach path is invoked.

### S2-P3 — Settings / Volume

Implement `SettingsService` and `SettingsViewController` for Volume only.
BOOT from Inbox enters; BOOT saves and returns to the same Inbox selection.
Thirteen stops are silent then 78–100 in increments of two. Apply the speaker
setting on change; show no numeric value. Preserve persistence fallback on save
failure, default 100, endpoint-wide ownership, RED ignored, and uninterrupted
Volume presentation through connectivity loss. Persistence must not block the
LVGL path; preserve required save ordering through service completion/state.

Tests: every stop and clamp/snap behavior, hardware application, save/re-entry,
restart restoration, failed save retaining the in-memory stop, BOOT and mute
save, RED ignored, and no status substitution while offline. Verifier checks
both the service result and controller teardown.

### S2-P4 — Message target

Implement the target service and controller. Show other hangout members only,
none initially selected; swipe changes focus independently of selection; tap
toggles selection. Follow the existing selected outline, selected-name header,
long-name truncation/`+N`, and empty `No people to message` treatment.

BOOT returns to the previous Inbox selection without creating a message.
RED with no valid recipients is ignored. RED with valid selection copies the
recipient identities into the navigation request and enters the existing opaque
`NoDestinationViewController`. Preserve the recording-route identity so the
later recording controller replaces the stand-in without changing picker intent.
No microphone capture, take, upload, ring-wipe, or send job starts in this slice.
The future recording ring-wipe is not simulated by the empty destination.

Tests: self exclusion, empty roster, toggle/deselect, focus versus selection,
roster removal of selected users, stale card events, long names, BOOT restoration,
RED invalid/valid, and immutable recipient payload after source view unload.
Verifier proves the stand-in creates no message or media side effects.

### S2-P5 — Runtime routes and integration

Generalize the queued navigation request to typed destinations and immutable
payloads. Resolve Login selection to real PIN, authentication success to Inbox,
Inbox BOOT to Settings, Inbox RED to targets, and valid targets to the empty
recording destination. Controllers request navigation; Root performs appearance.

Keep one runtime tick and phase order. Pump authentication/credential refresh,
Inbox and settings/target services independently of their loaded views, subject
to authentication and connectivity policy. Remove legacy signed-in fan-out from
the active path as services are connected. Consume stale button edges without
dispatching them to a later controller, preserving the completed runtime rule.

Reuse the defined stage motions and existing snapshot implementation. Add reverse
motion only where the applicable specification requires it; extend the coordinator
and test its snapshot ownership and callbacks. Lower-slot status changes retain
their specified in-place crossfade. No two live controller trees.

Tests: complete route chain, per-controller buttons, duplicate intent rejection,
busy transition input, mute during every route/transition, cooldown mute exception,
signed-out stale service results, credential refresh while Settings/targets/empty
destination are shown, and no return to the old global presenter.

### S2-P6 — Integrated device acceptance and memory

Run focused service and real-LVGL tests, repository checks, `make app:build`,
`make app:flash`, `make app:monitor`, and `make server` for the device run.
Retain build ID and serial evidence; operator observations establish visual and
physical-button acceptance.

Record load/unload/transition LVGL free/largest/high-water, capability-specific
heap readings, and snapshot lifetime for each new controller. Retain 32 KiB
initially. Investigate controller composition and allocations before proposing
a pool increase; any change needs explicit evidence and review.

Device acceptance:

1. Existing hardware boot order and hangout remain functional.
2. User selection shows PIN; incorrect PIN recovers; correct PIN shows that
   user's server Inbox. Cooldown and connection states follow the login spec.
3. Six messages and a larger inbox are browsable without pool exhaustion.
4. BOOT opens Volume, changes affect speaker settings, and BOOT restores Inbox
   selection. No playback is required for acceptance.
5. RED opens targets; self is absent; multiple recipients can be selected;
   BOOT restores Inbox; valid selection plus RED reaches the empty destination.
6. Empty Inbox and empty target list show their specified states.
7. Mute from every controller signs out, blanks, and rejects late authentication;
   unmute restores Login except while an existing PIN cooldown remains active.
8. Outgoing trees and snapshots are released; repeated journeys have stable
   memory recovery; no panic, stale handle, duplicated timer, or eager legacy UI.

## Order and boundaries

Implement and independently verify S2-P1 → S2-P2 → S2-P3 → S2-P4 → S2-P5 →
S2-P6. Packages may add their typed interfaces and host route fixtures before
the final runtime wiring; each must leave the build coherent.

Deferred: full Inbox audio/sketch playback, playback pixel leases/checkpoints,
recording/sketch canvas, sending/upload UI, profile editing, settings hub, and
storage-full policy. In-card playback remains the final design when playback
is added.

## Review item before execution

Choose the temporary Inbox Play-action presentation while playback is deferred.
All functional playback requirements remain assigned to a later slice. This
definition authorizes planning; it does not start implementation.
