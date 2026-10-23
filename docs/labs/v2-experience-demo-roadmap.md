# V2 experience demo and lab roadmap

Status: proposed lab work; no demo has been built or measured by this plan.  
Purpose: recreate x02's user journeys as separately testable v2 pieces before joining them in one client.  
Companion: [inbox, card reuse, and audio/sketch labs](inbox-carousel-playback-evaluation.md) (`LAB-01`–`LAB-05`).  
Layout and assistant workflow: [lab index](README.md), [repository layout](repository-layout.md), [assistant guide](assistant-guide.md).

## Product basis and limits

The active [login experience](../product/experiences/endpoint-login.md) defines much of readiness, roster, PIN, mute, and cooldown behavior. The [sketch experience](../product/experiences/sketch-record-and-playback.md) fixes the logical canvas, record geometry, and multicolor intent. The [device UI index](../product/device-ui/README.md#remaining-x02-coverage) lists signed-in and send screens still to author. The communication contracts for [endpoint health/login](../product/communication-contracts/endpoint-health-and-login.md), [message delivery](../product/communication-contracts/message-delivery.md), [inbox/media](../product/communication-contracts/inbox-and-media.md), [user profile](../product/communication-contracts/user-profile.md), and [user-scoped authority](../product/communication-contracts/user-scoped-envelope.md) contain proposed wire shapes and OPEN decisions.

Labs may compare proposed behavior and reveal feasibility. A lab result does **not** approve an OPEN product behavior, wire format, copy, or screen. Mark those scenarios exploratory until their owning experience or contract decides them. Use the [client standards](../standards/client-application-coding-standards.md) for ownership, bounded work, terminal outcomes, resource budgets, and verification levels.

## How to package each new demo

- Build a **new implementation** in active v2 lab code outside the read-only v1 archive (`client/labs/<slug>/` per [repository layout](repository-layout.md) and the [lab ID registry](README.md#lab-id-registry)). Give each demo its own ESP-IDF project, fixture manifest, trace schema, and assessment. The historical demo is reference material and may be run unmodified as a comparison; do not instrument it in place.
- Package any required **new server fixture implementation in the same demo version**. Its manifest identifies firmware build, server build or `none`, protocol/fixture revision, fault script, and media hashes. A changed server response or timing script creates a new pair version. Use isolated lab data and credentials, never a live household archive.
- Separate a model/virtual-clock test from a device run. A virtual clock can verify a 60-second or five-minute schedule quickly; the device run verifies actual task scheduling, display, input, and resource pressure. Record both levels accurately.
- Reuse the common capture fields and target measurement table in the [companion plan](inbox-carousel-playback-evaluation.md#common-method-and-evidence). Every scenario records stage and operation IDs, terminal event, UI update level, p50/p95/p99/worst latency, LVGL and pixel/media high-water and largest free block, queue depth/overflow, and a screenshot or screen video when visual behavior matters.
- For each lab, state **target**, **measured**, **concern/trigger**, and **decision** separately. Set numerical pass limits before running; no current demo PASS marker or static budget is a target-performance result.

## Proposed sequence

| ID | New demo to create | Product question | Paired server in demo version | Prerequisite |
|---|---|---|---|---|
| `LAB-06` | Stage shell and panel lifecycle | Can the 320×240 shell switch stages without stale handles, duplicate timers, or whole-screen data refreshes? | None; local fixture | Existing layout/panel specs; OPEN visuals remain variants |
| `LAB-07` | Readiness, roster, mute, and endpoint health | Can independent pollers keep a truthful, responsive login UI? | Required: versioned health/roster fault fixture | Decided login timing; proposed wire fields isolated in fixture |
| `LAB-08` | PIN operation and cooldown | Do retry, idempotency, and cancellation preserve one logical login? | Required: versioned PIN/idempotency fixture | `LAB-07` stage/reducer model |
| `LAB-09` | User session and late-event isolation | Can signed-in work be rejected after mute, sign-out, user switch, or session expiry? | Required: versioned session/inbox/media fixture | Session-issuance proposal; server authority decision remains OPEN |
| `LAB-10` | Record canvas and media budget | Can 240×240 capture map to 120×120 colored strokes while audio capture stays bounded? | None for local capture; versioned media fixture if upload/download is added | Sketch geometry decided; capture/send exceptions OPEN |
| `LAB-11` | Send, receipt, and recovery | Can one frozen capture reach a truthful terminal result through failure and lost responses? | Required: versioned send/receipt server fixture | `LAB-10`; recipient/atomicity/outbox choices OPEN |
| `LAB-12` | Profile/settings propagation (deferred from core V2) | Future profile-editing flow and cross-endpoint reconciliation | Only for a later profile/settings delivery; not a core V2 prerequisite | Profile editing and settings hub are deferred; core card rendering is decided in the profile contract |
| `LAB-13` | Full v2 journey composition | Do the components retain their guarantees when run together? | Required: one versioned integrated fixture compatible with all included operations | Focused labs and applicable product/contract decisions |
| `LAB-15` | Capture memory envelope | Can catalog/LVGL + full 180 s PCM retention fit without silent OOM? | None | [LAB-15 plan](lab-15-capture-memory-envelope.md); LAB-10 + LAB-14 reuse; **spec only** |

`LAB-02`–`LAB-05` in the [companion plan](inbox-carousel-playback-evaluation.md) cover large inboxes, card reuse, audio/sketch switching, format choices, and transition peak. Their results feed `LAB-13`; do not repeat their microbenchmarks as an opaque end-to-end PASS.

`LAB-15` answers the allocation gap called out for P7/P8 (full `pcm_s16le` buffer vs fonts/LVGL). It is independent of `LAB-13` and should complete before claiming a product capture path.

## LAB-06 — stage shell and panel lifecycle

**Create/run:** A local v2 stage controller using the [split-pane layout](../product/device-ui/layouts/LAYOUT-SPLIT-PANE.md), [panel module contract](../product/device-ui/patterns/PANEL-MODULE-CONTRACT.md), and [cover-down transition](../product/device-ui/patterns/TRANSITION-COVER-DOWN.md). Mount representative roster, PIN, empty-hangout, inbox, record, and receipt *test panels* with fixed synthetic data. Repeatedly enter/exit every stage; send progress, badge, and roster data changes while a stage is visible and while its exit animation runs. Distinguish approved screens from placeholders in the demo UI.

**Capture:** Stage/event transition trace; render level 0/1/2 per change; created/deleted/live widget counts; old/new tree overlap; callback/timer registrations; handle invalidations; build and animation p95/p99/worst; LVGL pool and pixel-buffer high-water; screen video.

**Result sought:** One session owner applies transitions, data changes do not rebuild the shell, repeated entry is idempotent, and no callback reaches a destroyed panel. Re-evaluate stage ownership or panel boundaries if a data event triggers Level 2, callbacks multiply after repeated entry, transition allocation fails, or old panel content appears in a new stage. The [missing-screen inventory](../product/device-ui/screens/MISSING.md) identifies placeholders that need product specifications before production implementation.

## LAB-07 — readiness, roster, mute, and endpoint health

**Create/run:** A new login-stage client plus paired server fixture that scripts Wi-Fi absence, delayed/invalid HTTP 200, empty/one/eight-user rosters, add/remove/reorder, two failed checks, Wi-Fi return, and mute/unmute. Use a virtual clock for the decided active **60-second** roster refresh, idle **five-minute** refresh after ten idle minutes, one-second request timeout, and independent **60-second** health heartbeat; then repeat representative cases on device with real time. Keep health truth distinct from a signed-in person's presence.

**Capture:** Input and animation gaps while requests time out; request/response and retry timestamps; server health-beat receipt and stale-state trace; roster revision/user IDs, local recency order and focused ID; stage before/after each result; ignored stale results; queue capacity and overflow; screen video for connecting, Wi-Fi unavailable, empty hangout, and roster.

**Result sought:** The connecting animation and touch remain responsive during network waits; only a valid readiness response exposes selectable cards; roster reconciliation preserves identity/focus; health continues while muted and does not change user stage. Re-evaluate if a timeout blocks presentation, a cached/invalid roster appears ready, a background response replaces partial PIN, refreshes multiply after Wi-Fi return, heartbeat cadence follows roster activity, or eight users exceed the UI budget. Check actual expected outcomes against [EXP-LOGIN scenarios](../product/experiences/endpoint-login.md#8-acceptance-scenarios-and-evidence) and keep unresolved response validity/stale-clock details OPEN in the [contract](../product/communication-contracts/endpoint-health-and-login.md#7-traceability-and-open-decisions).

## LAB-08 — PIN operation, retry, and cooldown

**Create/run:** A new PIN pad/session reducer with a paired idempotency server. Script a correct PIN, wrong PIN, five wrong attempts, one or all timed-out attempts, first success with lost response, duplicate exact request, stale success after mute, selected-user removal, partial-entry 60-second idle timeout, and BOOT at zero/partial/four digits. Use the decided four one-second attempts with 1/2/4-second delays for transport timeout; use the same key and exact request for retries. Isolate concurrent-key and mismatched-key behavior as exploratory until the wire contract decides it.

**Capture:** Key/request fingerprint, attempt number and timestamp, server side-effect count and stored result, client operation generation, one terminal event, stage and visible copy, cooldown counter/remaining time, input callback latency, and late-result rejection. Mask PIN values in all evidence.

**Result sought:** One submission causes at most one remote login side effect and one accepted local terminal outcome; timeout never counts as wrong PIN; five wrong credentials produce the decided cooldown; mute cannot be undone by a late success. Re-evaluate if retry sends a new key, a lost response creates a second login, a poller clears PIN state, `checking...` persists without a terminal event, or the UI freezes for any network attempt. See the [login experience](../product/experiences/endpoint-login.md#pin-login) and [health/login contract](../product/communication-contracts/endpoint-health-and-login.md#3-operation-inventory-and-decided-rules).

## LAB-09 — signed-in authority and late-event isolation

**Create/run:** A small signed-in client and paired server fixture that can issue, expire, and reject a **proposed** user-scoped session. Begin inbox fetch, media fetch, playback-state write, or profile update for user A; then mute, sign out, or sign in as B before the result returns. Send duplicate/out-of-order push hints and an unauthorized request naming B with A's credential. Record alternative session revocation behavior separately until approved.

**Capture:** Endpoint/user/session and operation IDs, server actor derived from credentials, request authorization result, result application/rejection, cache/media buffer cleanup, queue backlog, and time from mute to display-off and local cancellation. No raw credentials in traces.

**Result sought:** No A result or cached media becomes visible in B's session; request fields cannot select another actor; a late remote success cannot restore local login after mute. Re-evaluate if a stale response changes stage, focus, read position, profile, or media view, or if server authorization depends on a caller-supplied user ID. Session issuance/revocation and error mapping are still [OPEN](../product/communication-contracts/user-scoped-envelope.md#invariants-and-open-items); this demo tests alternatives and does not approve a token protocol.

## LAB-10 — record canvas and media budget

**Create/run:** A local capture demo with the owner-decided **240×240 display canvas**, **80×240 rail**, and **120×120 logical coordinates**. Draw sparse/dense multicolor strokes, background-colored strokes, edge strokes, rapid color switches, and no-stroke audio. Repeat start/stop/cancel and inject mic/codec/allocation failure. Compare a bounded stream/chunk capture with a whole-recording buffer as a measured control. Do not add a timer, record disk, recipient flow, or send UX merely because v1 had one.

**Capture:** Touch-to-ink latency, logical/display coordinate agreement, missed/coalesced points, audio continuity, retained and transient PCM/sketch/framebuffer bytes, largest allocation, internal/PSRAM high-water and largest free block, capture worker load, stop/cancel cleanup time, and final format bytes. Include a 2× display-versus-card replay image comparison.

**Result sought:** Colors, timing, and geometry survive capture without unbounded copies or UI stalls. Re-evaluate if the 240×240 image plus audio and packing copies exceed the memory envelope, drawing takes a full-screen invalidation per point with visible lag, cancellation leaves codec/buffers live, or no-stroke audio is incorrectly rejected. The [sketch experience](../product/experiences/sketch-record-and-playback.md#6-requirements) decides geometry; the [record/send assessment](../v1-assessments/recording-send.md) identifies the v1 duplicate-buffer risk.

## LAB-11 — send, receipt, and recovery

**Create/run:** A new send-operation client and paired server fixture with one immutable client message ID, frozen sender/recipient/media input, part acknowledgements, and a receipt/status lookup. Script one recipient, several recipients, invalid recipient before completion, offline upload, dropped part acknowledgement, dropped accepted receipt, push failure after commit, restart during upload, storage full, and cancel at each phase. Compare candidate chunk/stream limits and media bytes. Use exploratory labels for atomic recipient delivery, outbox retention, retry, and user copy until the [delivery contract](../product/communication-contracts/message-delivery.md#proposed-v2-json-and-media-schemas) and a send experience decide them.

**Capture:** Capture-to-upload/receipt timings; peak old+new buffers and largest free block; bytes sent/retried; client ID, part sequence/hash, server durable part count, accepted message and recipient count, receipt lookup result, push result, exactly-one terminal event, work retained/discarded on failure, and user-visible phase updates.

**Result sought:** The client reports `Sent` only after the chosen acceptance condition; lost responses recover without duplicate recipient delivery; progress text updates locally without rebuilding a scene; all failure/cancel paths close resources and reach a defined outcome. Re-evaluate if a partial multi-recipient send is shown as simple failure, a retry duplicates work, receipt lookup cannot distinguish open from absent, capture is silently lost, or upload blocks rendering. The unresolved policies are [COM-OPEN-001–005](../product/communication-contracts/message-delivery.md#proposed-typed-outcomes-and-checks), not defaults for the demo to settle.

## LAB-12 — profile and settings propagation (deferred from core V2)

**Scope:** This lab is deferred from the initial V2 implementation boundary, which excludes profile editing and the full settings hub. The core V2 decision is that inbox and roster cards use `card_background_id` and do not render `profile_accent`; see the [profile contract](../product/communication-contracts/user-profile.md#2-decided-product-meaning). The profile contract also records failed-save reversion and last-write-wins. This lab is future follow-on work if profile editing is brought into scope.

**Create/run (when brought into scope):** First, a local visual demo of the later settings experience, modal focus, color/avatar previews, and return to inbox. Then add a paired server fixture for partial profile updates, cross-endpoint convergence, lost response, missed hint, invalid asset/color, and offline choice. Keep sketch stroke color separate from profile accent and card background. Do not treat alternate accent-rendering behaviors as open product branches for core V2.

**Capture:** Preview versus server-acknowledged state, field-level request/revision, duplicate operation handling, convergence and refetch result, asset fallback, focus and overlay lifetime, local/remote copy, and confirmation that a profile hint updates canonical profile/card-background data without painting `profile_accent` onto core V2 cards.

**Result sought:** A failed save is not presented as saved; one endpoint cannot silently overwrite another's edit; visual updates preserve focus and do not rebuild the whole inbox. Re-evaluate if optimistic color persists after rejection, old profile responses overwrite new revisions, a missing avatar breaks layout, or profile accent and sketch stroke color are conflated. The profile contract's remaining appearance/catalog details and wire transport/retention are follow-on work; the settings hub stays deferred until specified.

## LAB-15 — capture memory envelope

**Create/run:** See [lab-15-capture-memory-envelope.md](lab-15-capture-memory-envelope.md). Compose LAB-10 capture limits with a LAB-14-weight UI montage. Scripted runs use a fake clock to `duration_cap` and a deterministic PCM fill. Optional Kconfig arms link Opus (flash size + runtime) or SDMMC spill (exploratory).

**Capture:** `resource_sample` and `allocation_attempt` rows at UI mount, after 5,760,000-byte PCM reserve, and at duration cap; internal, PSRAM, and LVGL largest-free blocks.

**Result sought:** Explicit `allocation_failure` terminal if the desk cannot reserve the owner wire maximum—not undefined behavior. A passing experiment records numbers for owner decision on storage medium and codec; it does not approve Opus on the delivery contract.

## LAB-13 — composed v2 journey

**Create/run:** Integrate only behaviors with decided contracts into a new v2 shell: boot muted/unmuted → readiness/roster → PIN → inbox → play → record/send candidate → receipt → inbox → mute/sign-out. Use one versioned server fixture for all included requests and media. Run normal flow and controlled overlaps: roster refresh during PIN, inbox push during snap/play, send while inbox state changes, media fetch during mute, server outage/recovery, and repeated stage cycles. Include at least eight roster users and the 8/16/64/256 inbox fixtures where paging is supported.

**Capture:** The full common trace and all resource peaks across stage overlap; latency/underruns/sketch lag from [LAB-03](inbox-carousel-playback-evaluation.md#lab-03--audiosketch-switching-and-pause); contract outcomes and server-side side-effect counts; UI video; and a scenario-by-scenario verification level. Compare each measure with its focused lab result to identify integration overhead.

**Result sought:** Composition preserves the focused labs' ownership, identity, bounded-work, and terminal-outcome guarantees. Re-evaluate the architecture if a combined journey introduces new blocking callbacks, unbounded buffers, stale cross-user data, duplicate send/login effects, full-screen refresh on data events, or materially worse frame/audio results. An OPEN signed-in or send behavior remains a labeled experimental branch and cannot be claimed as implemented product parity.

## Promotion to implementation

For each demo, file a short report under [`docs/labs/evidence/`](evidence/README.md) with its firmware/server pair, fixture manifest, raw evidence, measurement table, observed result, and recommended change. Promote stable modules to [`client/shared/`](repository-layout.md#shared-client-code) before composing them in [`client/device/`](../../client/device/). Link the report from the affected experience, communication contract, or UI assessment. Promote a measured limit to the [LVGL budget foundation](../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md) only after the target run and owner review required there. A lab may establish feasibility or reject a design; product and wire decisions still follow their own authoring workflow.
