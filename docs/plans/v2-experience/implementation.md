# V2 core journey — implementation map

Status: **preview — spec incomplete** (published 2026-10-22) — companion to the experience specifications under [docs/product/experiences/](../../product/experiences/). There is no plan-bundle `spec.md`. This preview treats those experience specifications as the normative behavior source for one slice: login, inbox and playback, record/send, and Volume. It does not grant the implementation authorization those specs withhold, and it does not approve wire. Refresh with `/spec-implementation-map` after the operator accepts this boundary or after a material spec or code change.

Canonical map for this bundle. Not ready for orchestration until the wire gate in **P1** is accepted.

Slice for this pass:

- In: [endpoint login](../../product/experiences/endpoint-login.md) (`EXP-LOGIN`), [message carousel and playback](../../product/experiences/message-carousel-and-playback.md), [sketch record and playback](../../product/experiences/sketch-record-and-playback.md), [message recording and sending](../../product/experiences/message-recording-and-sending.md) (`EXP-MESSAGE-RECORD-SEND`), [Volume](../../product/experiences/settings.md) (`EXP-SETTINGS`).
- Out: profile editing, the settings hub, and `LAB-12`.
- Wire: decided product meaning is in scope. Routes, token format, codecs, chunking, and HTTP status numbers stay gap work in **P1**.

## Spec alignment

### Strengths (spec)

- Each in-scope experience has owner-decided behavior, acceptance rows, and a follow-on table that already separates product meaning from engineering wire.
- Login, carousel, send, and Volume name the physical controls (mute, BOOT, red record) and the handoffs between them, so stage ownership can be drawn before firmware.
- Volume’s included and excluded lists keep the settings hub and profile editing out of this delivery.
- The profile contract already decides the core-journey card rule: roster and inbox faces use `card_background_id` and do not render `profile_accent`.
- Device UI already composes the signed-out screens and the record canvas. The [lab roadmap](../../labs/v2-experience-demo-roadmap.md) matches this slice (`LAB-06` through `LAB-11`, then `LAB-13`) and already defers `LAB-12`.

### Strengths (repository)

- Product firmware exists and identifies itself. [client/device/main/app_main.c](../../../client/device/main/app_main.c) logs `V2_BOOTSTRAP_READY` and does not mount a journey.
- Lab evidence already exercises pieces of the slice without being the product app: stage shell (`LAB-06`), readiness and roster (`LAB-07`), bounded inbox (`LAB-02`), audio/sketch playback (`LAB-03`), sketch format comparison (`LAB-04`), transition peak model (`LAB-05`), record canvas (`LAB-10`), and the flashable UI catalog (`LAB-14`).
- Login screens and the record screen are in the device UI catalog and can be flashed with `make lab.lab14`.
- Eight card-background fills are selected in [FOUNDATION-CARD-BACKGROUNDS](../../product/device-ui/foundations/FOUNDATION-CARD-BACKGROUNDS.md). [COMP-USER-CARD](../../product/device-ui/components/COMP-USER-CARD.md) and [COMP-MESSAGE-CARD](../../product/device-ui/components/COMP-MESSAGE-CARD.md) already point card faces at `card_background_id`.
- [Client application coding standards](../../standards/client-application-coding-standards.md) and [v2 implementation practices](../../v1-assessments/v2-implementation-practices.md) already require one session owner, typed operation outcomes, and Level 0/1/2 UI updates.

### Gaps and clarifications before build

| Topic | Present state | Spec requirement | Suggested resolution |
|-------|---------------|------------------|----------------------|
| Plan bundle | Experience specs say no implementation authorization. This file is a preview map, not an approved `spec.md`. | A map the orchestrator can staff. | Keep this file as **preview** until accepted. Acceptance of the map still does not approve wire. |
| Approved wire | Contracts state that conceptual `/v2/` paths, session credential placeholders, codecs, chunking, retention, and HTTP maps are proposals. | Login, inbox, and send behavior that depends on a request or response. | **P1** accepts an engineering wire revision. Later packages consume that revision. They do not invent routes inside firmware. |
| Product server root | Active docs place lab fixtures under `server/labs/`. No product server tree is named. | A server that can issue sessions, rosters, inboxes, and send receipts for the accepted schemas. | **P1** keeps the first accepted-schema server as a versioned lab fixture. Naming a product server root is a separate decision. |
| Signed-in and send screens | [screens/MISSING.md](../../product/device-ui/screens/MISSING.md) and [panels/MISSING.md](../../product/device-ui/panels/MISSING.md) are 2026-10-11 inventories. Inbox, recipient picker, send progress/result, and Volume have no `SCR-*`. Login and record screens exist as drafts. | Carousel, send, and Volume each name a visible stage. | **P2** authors those screens from the 2026-10-21 experience specs. Treat the MISSING inventories as stale checklists. |
| Volume geometry and storage | Volume stop set, copy, and leave/save behavior are decided. Slider pixels and the persist mechanism are follow-on in the Volume spec. | `EXP-SETTINGS` `EXP-011` through `EXP-017`. | **P2** may leave slider pixels as `UI-OPEN` if the owner has not picked them. **P9** implements the 13 stops and save-on-leave without painting a codec number. |
| Avatar catalog | [FOUNDATION-PROFILE-AVATARS](../../product/device-ui/foundations/FOUNDATION-PROFILE-AVATARS.md) says artwork is deferred and LAB-14 initials are the interim display. Working-tree portrait sources are not listed in that catalog. [COMP-USER-CARD-BUST](../../product/device-ui/components/COMP-USER-CARD-BUST.md) is pending owner review. | Identity surfaces show a catalog avatar, with initials as the interim fallback. | Core-journey packages use the initials fallback. Do not promote working-tree portraits or the bust treatment until the foundation lists them. |
| `client/shared/` | [Repository layout](../../labs/repository-layout.md) projects `client/shared/` for promoted modules. That directory is absent. | Lab modules are promoted before they are composed into `client/device/`. | **P3** creates `client/shared/` when the first shell module is promoted. Labs stay disposable. |
| Lab index drift | [docs/labs/README.md](../../labs/README.md) still says only `LAB-02`, `LAB-03`, `LAB-05`, and `LAB-14` are registered. [config/labs.registry.yaml](../../../config/labs.registry.yaml) also registers `lab04`, `lab06`, `lab07`, `lab10`, and `lab12`. [client/labs/README.md](../../../client/labs/README.md) still says the tree is documentation-only. | Verifiers need the real entrypoints. | Trust the registry and each lab README. **P3** does not implement labs that already exist. A docs fix for the stale index can ride with **P3** or stay a separate cleanup. |
| Send `EXP-015` | `EXP-MESSAGE-RECORD-SEND` has no `EXP-015` bullet (42 acceptance rows covering `EXP-001`–`EXP-014` and `EXP-016`–`EXP-043`). | — | Do not invent `EXP-015`. |
| Profile editing | `LAB-12` firmware and a lab server exist and are registry-registered. The Volume spec and the lab roadmap exclude them from core V2. | Card rendering uses `card_background_id`. Profile save, accent palette, and the settings hub stay out. | No package promotes `LAB-12`. Roster and inbox cards read `card_background_id` once **P1** names the field. |

### Acceptance criteria traceability

Experience IDs reuse `EXP-001`. The experience prefix is required.

| AC | Present | Delta / work |
|----|---------|----------------|
| `EXP-LOGIN` `EXP-001` `EXP-002` `EXP-003` `EXP-027` `EXP-033` `EXP-034` `EXP-042` `EXP-044` | Mute and cooldown behavior are specified. Product firmware has no mute path. `LAB-07` probes roster and mute as a lab. | **P4** signed-out mute, boot latch, and cooldown survival. **P5** rejects a late session after mute. **P1** for invalidate route and retry count (`COM-OPEN-009`). |
| `EXP-LOGIN` `EXP-004` `EXP-005` `EXP-011` `EXP-023` `EXP-024` `EXP-037` `EXP-038` `EXP-046` | Copy, timeouts, and lower-slot status are decided. Login screens and `PANEL-STATUS-MESSAGE` exist as drafts. Field validity is a contract proposal. | **P4** client behavior. **P1** roster schema and HTTP map (`COM-OPEN-004`, `COM-OPEN-005`). Anti-flicker hold stays an engineering choice inside **P4**, called out in the package, not a new product rule. |
| `EXP-LOGIN` `EXP-006` `EXP-007` `EXP-008` `EXP-009` `EXP-013` `EXP-019` `EXP-020` `EXP-025` `EXP-026` `EXP-028` `EXP-032` `EXP-048` | Roster order, eight-user minimum, and empty copy are decided. `SCR-SIGN-IN-ROSTER` and `SCR-EMPTY-HANGOUT` are drafts. Initials fallback is the interim avatar. | **P4**. Eight-user layout uses the existing roster carousel. Avatars stay on the initials fallback until the foundation catalog changes. |
| `EXP-LOGIN` `EXP-014` `EXP-015` `EXP-016` `EXP-017` `EXP-018` `EXP-021` `EXP-022` `EXP-029` `EXP-030` `EXP-031` `EXP-036` `EXP-039` `EXP-040` `EXP-045` `EXP-047` `EXP-049` `EXP-052` | PIN, cooldown, unknown-user, and idempotency meanings are decided. `SCR-PIN-ENTRY` and `SCR-PIN-COOLDOWN` are drafts. No `LAB-08` tree. | **P4**. **P1** for PIN route, token bytes, and restart durability (`COM-OPEN-006`–`COM-OPEN-008`). |
| `EXP-LOGIN` `EXP-035` `EXP-041` `EXP-043` `EXP-051` | Health-beat meaning is decided, including mute and skipped missed beats. | **P4** scheduler. **P1** route, credential, and missed-beat clock (`COM-OPEN-001`–`COM-OPEN-003`). |
| `EXP-LOGIN` `EXP-010` `EXP-012` `EXP-050` | Session issuance without inbox content is decided. The envelope’s token format is still a proposal. | **P4** performs the login transition. **P5** is the only owner of the signed-in credential. **P1** token format (`COM-OPEN-006`, envelope `COM-OPEN-001`). |
| Carousel `EXP-001`–`EXP-015`, `EXP-018`–`EXP-024`, `EXP-038`–`EXP-043` | Playback, order, empty copy, sketch-on-card, and header transport are decided. No inbox screen or message carousel panel. `LAB-02` and `LAB-03` are lab evidence. Acoustic evidence for `LAB-03` is incomplete. | **P2** inbox screen. **P6** product carousel. **P1** inbox/media schemas (`COM-OPEN-001`–`COM-OPEN-005` on [inbox and media](../../product/communication-contracts/inbox-and-media.md)). |
| Carousel `EXP-016` `EXP-017` `EXP-027`–`EXP-033` | Signed-in connectivity replaces the lower slot and disables Play. Login already defines the status panel. | **P6**, reusing the login status panel. Cached playback during outage stays forbidden. |
| Carousel `EXP-025` `EXP-026` `EXP-036` `EXP-037` | Mute discards play heads. BOOT opens Volume and pauses audio. Red record starts send. | **P5** mute. **P9** BOOT handoff. **P8** red-record handoff. |
| `EXP-SKETCH-RECORD-PLAYBACK` `SK-001`–`SK-009`, `EXP-SKP-001`–`EXP-SKP-009` | Geometry, rail, no eraser, and hidden card chrome during sketch are decided. `SCR-RECORD-SKETCH` exists. `LAB-10` captures locally and does not send. `LAB-04` compares formats and does not pick one. | **P7** record path. **P6** card replay (`EXP-SKP-002`, `EXP-SKP-009`). **P1** RGB565 packing and sketch bytes (`OPEN-SKP-01`, `OPEN-SKP-03`, delivery `COM-OPEN-002`). |
| `EXP-MESSAGE-RECORD-SEND` `EXP-001`–`EXP-014`, `EXP-016`–`EXP-043` | Recipient rules, capture, `preparing` / `sending` / `sent`, queue, refusal-versus-wait, and no sender self-copy are decided. No picker or send screen. No `LAB-11`. No `EXP-015`. | **P2** picker and send screens. **P7** capture. **P8** send and queue. **P1** delivery wire (`COM-OPEN-002`, `COM-OPEN-004`–`COM-OPEN-006`). **P5** so a muted or switched user cannot upload. |
| `EXP-SETTINGS` `EXP-001`–`EXP-017` | Volume behavior is confirmed. No Volume screen. Hub and profile edit are excluded. | **P2** Volume screen. **P9** slider, desk persist, and inbox return. |

---

## Repository layout

Product firmware is a bootstrap. Journey behavior lives in labs and in specifications. `client/shared/` is projected and absent. There is no product server tree.

| Path | Role |
|------|------|
| [client/device/main/app_main.c](../../../client/device/main/app_main.c) | Product firmware entry. Logs `V2_BOOTSTRAP_READY`. |
| [client/labs/](../../../client/labs/README.md) | One ESP-IDF project per lab. Not the shipping app. |
| [server/labs/](../../../server/labs/README.md) | Lab fixtures only (`lab-02-paging`, `lab-03-media`, `lab-07-readiness-roster`, `lab-12-profile-settings`). |
| [config/labs.registry.yaml](../../../config/labs.registry.yaml) | Host entrypoints for `make lab.*`. |
| [docs/product/experiences/](../../product/experiences/) | Normative behavior for this preview. |
| [docs/product/communication-contracts/](../../product/communication-contracts/) | Decided meaning plus unapproved wire. |
| [docs/product/device-ui/](../../product/device-ui/README.md) | Visual specs and catalog. Login and record screens exist. Inbox, picker, send, and Volume do not. |
| [docs/labs/v2-experience-demo-roadmap.md](../../labs/v2-experience-demo-roadmap.md) | Lab sequence. Evidence, not product approval. |
| [docs/standards/client-application-coding-standards.md](../../standards/client-application-coding-standards.md) | Ownership, budgets, verification ladder L0–L3. |

---

## Workstreams mapped to decisions

### EXP-LOGIN — signed-out readiness, roster, PIN, health

**Present:** Behavior through `EXP-052` is owner-decided. Screens `SCR-SIGN-IN-ROSTER`, `SCR-EMPTY-HANGOUT`, `SCR-PIN-ENTRY`, and `SCR-PIN-COOLDOWN` are drafts in the catalog. `LAB-07` has firmware, a roster fixture server, and a software-qualified run. Product firmware does not speak HTTP. Health, roster, and PIN routes are proposals in [endpoint health and login](../../product/communication-contracts/endpoint-health-and-login.md).

**Required:** One signed-out reducer applies readiness, roster, PIN, cooldown, and mute. Selectable cards appear only after a valid roster. PIN retries use one operation key. Health continues while muted and does not change the login stage.

| Resource | Role |
|----------|------|
| [endpoint-login.md](../../product/experiences/endpoint-login.md) | Acceptance source |
| [endpoint-health-and-login.md](../../product/communication-contracts/endpoint-health-and-login.md) | Meaning decided; wire open |
| [client/labs/lab-07-readiness-roster/](../../../client/labs/lab-07-readiness-roster/README.md) | Lab evidence for roster and mute probes |
| [server/labs/lab-07-readiness-roster/](../../../server/labs/lab-07-readiness-roster/server.py) | Lab fixture, not the accepted product schema |

**Risks:** Promoting `LAB-07` in place would bake the lab fixture schema into the product. PIN values must stay out of traces.

### Session authority — credential bound to endpoint and user

**Present:** Login `EXP-050` and the [user-scoped envelope](../../product/communication-contracts/user-scoped-envelope.md) decide that the server derives the actor from a credential issued at PIN success, and that mute invalidates it. Token bytes, expiry, and refresh are follow-on. No `LAB-09` tree. No `client/shared/`.

**Required:** Inbox, media, last-viewed, and send calls use that credential. A late result for user A cannot paint user B. Mute and sign-out reject in-flight work locally even when invalidate is still retrying.

| Resource | Role |
|----------|------|
| [user-scoped-envelope.md](../../product/communication-contracts/user-scoped-envelope.md) | Proposed envelope |
| [v2-implementation-practices.md](../../v1-assessments/v2-implementation-practices.md) | One reducer; workers return typed events |

**Risks:** Lost invalidate (`COM-OPEN-009`) can leave a server session alive. The client must still stay signed out.

### Carousel and sketch playback

**Present:** Carousel acceptance rows are decided, including no offline audio playback and sketch-hides-portrait. Inbox screen and inbox body panel are not authored. `LAB-02` pages a summary inbox. `LAB-03` plays fixed fixtures; first-audible and underrun evidence is incomplete. `LAB-04` does not choose a sketch encoding.

**Required:** Signed-in inbox on the split pane, stable message identity, Level 1 updates for arrival and badges, header Play/Pause, and 120×120 sketch replay on the card.

| Resource | Role |
|----------|------|
| [message-carousel-and-playback.md](../../product/experiences/message-carousel-and-playback.md) | Acceptance source |
| [inbox-and-media.md](../../product/communication-contracts/inbox-and-media.md) | Meaning decided; codecs and snapshot limits open |
| [client/labs/lab-02-bounded-inbox/](../../../client/labs/lab-02-bounded-inbox/README.md) | Card-window lab |
| [client/labs/lab-03-media-playback/](../../../client/labs/lab-03-media-playback/README.md) | Playback lab |

**Risks:** A full inbox rebuild on each snapshot repeats the v1 failure mode. Device paging, if introduced for memory, still has to reach every current server message (carousel `EXP-035`).

### Record and send

**Present:** Send acceptance rows are decided, including frozen recipients, keep-versus-discard, and no sender copy. `SCR-RECORD-SKETCH` and `LAB-10` cover local capture. Picker and send screens are not authored. No `LAB-11`. Delivery routes, audio codec, sketch format, chunk size, and HTTP status numbers are open.

**Required:** Red from the inbox opens recipient selection. A valid selection opens the 240×240 canvas. Finish becomes one `preparing` → `sending` → `sent` sequence, or a queued wait, or `couldn't send`. One `client_message_id` does not create a second message.

| Resource | Role |
|----------|------|
| [message-recording-and-sending.md](../../product/experiences/message-recording-and-sending.md) | Acceptance source |
| [message-delivery.md](../../product/communication-contracts/message-delivery.md) | Refusal-versus-wait decided; wire open |
| [client/labs/lab-10-record-canvas/](../../../client/labs/lab-10-record-canvas/README.md) | Local capture lab, no network |

**Risks:** `LAB-10` screen outcomes (`Completed`, `Stopped`) are lab presentation, distinct from the product `preparing` / `sending` / `sent` copy. Power loss may drop a finished queue (send `EXP-035`); storage medium is still delivery `COM-OPEN-003`.

### Volume

**Present:** `EXP-001`–`EXP-017` are confirmed. The hub is named and excluded. No Volume screen, slider component, or desk persist module.

**Required:** BOOT from the signed-in inbox opens Volume, pauses playback, and stays signed in. BOOT returns to the same inbox selection. Mute saves, then signs out. The speaker follows the slider immediately. Thirteen stops, no on-screen number.

| Resource | Role |
|----------|------|
| [settings.md](../../product/experiences/settings.md) | Volume source |
| [FOUNDATION-DEVICE-CANVAS](../../product/device-ui/foundations/FOUNDATION-DEVICE-CANVAS.md) | Slider track minimum, not the Volume screen |

**Risks:** Implementing the old four-button settings home would contradict the Volume delivery. Slider pixel geometry is still follow-on.

---

## Orchestration packages

| ID | Title | Surfaces | Depends on | Primary paths | Verify (AC / tests) |
|----|-------|----------|------------|---------------|---------------------|
| P1 | Accept core-journey wire | docs, config | — | Communication contracts; a versioned fixture under `server/labs/` | Contract `COM-OPEN` rows named below. No product AC is done by docs alone. |
| P2 | Author missing core screens | docs, host-ui | — | `docs/product/device-ui/screens/`, `panels/`, `catalog/scenes/` | Screen checklist against carousel, send, and Volume acceptance rows. `make lab.lab14` for catalog scenes. |
| P3 | Promote a product stage shell | code | — | `client/shared/` (new), `client/device/`, patterns from `client/labs/lab-06-stage-shell/` | `LAB-06` regression stays green. Product app boots past `V2_BOOTSTRAP_READY` into an empty shell. L0/L1. |
| P4 | Signed-out login | code | P1, P3 | Product session reducer; login screens; health/roster/PIN client | `EXP-LOGIN` rows in the traceability table. L1 model plus L2 scripted fixture. |
| P5 | Signed-in credential and late-result fence | code | P1, P4 | Session module used by later calls | `EXP-LOGIN` `EXP-010`, `EXP-012`, `EXP-042`, `EXP-050`. Carousel `EXP-021`, `EXP-025`. Send `EXP-032`, `EXP-033`. |
| P6 | Inbox carousel and playback | code, host-ui | P1, P2, P5 | Inbox screen; playback worker; `LAB-02` / `LAB-03` as reference only | Carousel `EXP-001`–`EXP-043` except BOOT and red-record handoffs owned by P9 and P8. |
| P7 | Record canvas in the product shell | code | P3 | Record screen; capture module promoted from `LAB-10` behavior | `EXP-SKP-001`–`EXP-SKP-008`. Send `EXP-011`, `EXP-012`, `EXP-016`, `EXP-017`, `EXP-021`, `EXP-022`, `EXP-037`–`EXP-039`. |
| P8 | Send, receipt, and queue | code | P1, P2, P5, P7 | Send screens; outbox; delivery client | Send `EXP-001`–`EXP-043` except the capture rows owned by P7. Delivery `COM-013` classes. |
| P9 | Volume | code, host-ui | P2, P6 | Volume screen; desk persist; speaker | `EXP-SETTINGS` `EXP-001`–`EXP-017`. Carousel `EXP-036`. |
| P10 | Composed journey | code, tooling | P4, P5, P6, P8, P9 | `client/device` integration; one fixture compatible with P1 | Cross-stage races in the lab roadmap’s `LAB-13` list. No new product rules. |

P7 may start once P3 exists. It does not wait for P4. P8 waits for P7 and P5. P9’s inbox handoff waits for P6. The local stop table can be unit-tested earlier.

### Package briefs (for implementer agents)

#### P1 — Accept core-journey wire

- **Goal:** Freeze engineering choices the contracts already mark as follow-on, without reopening owner-decided behavior.
- **Deliverables:** Accepted route names, schema ids, session-credential format, HTTP status map, health missed-beat clock, roster retry classes, PIN result retention across restart, lost-invalidate retry bound, inbox snapshot bounds, audio codec, sketch byte layout, send part size and ack rules, receipt retention. A fixture server and fixture manifest that speak only those schemas. Update the contract follow-on tables so each accepted row says what was chosen.
- **Out of scope:** Product copy, card colors, avatar art, profile editing, Volume stops, and any firmware in `client/device`.
- **Touchpoints:** [endpoint-health-and-login.md](../../product/communication-contracts/endpoint-health-and-login.md) `COM-OPEN-001`–`009`; [user-scoped-envelope.md](../../product/communication-contracts/user-scoped-envelope.md); [inbox-and-media.md](../../product/communication-contracts/inbox-and-media.md) `COM-OPEN-001`–`005`; [message-delivery.md](../../product/communication-contracts/message-delivery.md) `COM-OPEN-002`, `004`–`006`; sketch `OPEN-SKP-01` and `OPEN-SKP-03`.

#### P2 — Author missing core screens

- **Goal:** Compose the signed-in visuals the experience specs already describe.
- **Deliverables:** `SCR-MESSAGE-INBOX` (header identity, Play/Pause, message carousel, empty and connectivity states). Recipient picker screen and panels. Send progress/result screen for `preparing`, `sending`, `sent`, `couldn't send`, `no Wi-Fi`, and `connecting`, including `+N` overflow. Volume screen: title `Volume`, slider, no numeric readout. Catalog scenes and a `LAB-14` renderer path for each new panel. `UI-OPEN` only where the experience spec still delegates (Volume slider pixels, cooldown type fit).
- **Out of scope:** Retry code, persist format, profile hub, bust portraits, and changing owner-decided copy.
- **Touchpoints:** [screens/MISSING.md](../../product/device-ui/screens/MISSING.md), [panels/MISSING.md](../../product/device-ui/panels/MISSING.md), [catalog/README.md](../../product/device-ui/catalog/README.md), carousel and send and Volume specs.

#### P3 — Promote a product stage shell

- **Goal:** Give `client/device` one session writer, cover-down transitions, and panel mount/unmount that survives repeated entry.
- **Deliverables:** `client/shared/` module for the shell; `client/device` mounts it; data updates do not rebuild the shell; destroyed panels receive no callbacks. Lab `LAB-06` stays a lab.
- **Out of scope:** Real roster, PIN, inbox, or audio.
- **Touchpoints:** [LAYOUT-SPLIT-PANE](../../product/device-ui/layouts/LAYOUT-SPLIT-PANE.md), [PANEL-MODULE-CONTRACT](../../product/device-ui/patterns/PANEL-MODULE-CONTRACT.md), [TRANSITION-COVER-DOWN](../../product/device-ui/patterns/TRANSITION-COVER-DOWN.md), [lab-06](../../../client/labs/lab-06-stage-shell/README.md).

#### P4 — Signed-out login

- **Goal:** Implement `EXP-LOGIN` on the product shell against the P1 fixture.
- **Deliverables:** Readiness and roster pollers off the UI thread; in-memory recency; PIN reducer with one operation key; cooldown clock that survives mute and connectivity loss; health beats that ignore user identity; lower-slot status crossfade. Eight-user roster. Initials avatar. Cards use `card_background_id`.
- **Out of scope:** Inbox contents in the login response, profile editing, token design (consumed from P1), anti-flicker hold recorded as an engineering constant with a test, not a new product decision.
- **Touchpoints:** Login experience, login screens, `LAB-07` as evidence only.

#### P5 — Signed-in credential and late-result fence

- **Goal:** One credential binds endpoint and user. Later packages call through this fence.
- **Deliverables:** Issue on PIN success, clear on mute, reject stale completions, best-effort invalidate that does not block mute. Inbox and send workers take a generation and drop mismatched results.
- **Out of scope:** Inbox UI, upload bytes, profile patch.
- **Touchpoints:** Envelope contract, login `EXP-050` and `EXP-042`, carousel `EXP-021` and `EXP-025`, send `EXP-032` and `EXP-033`.

#### P6 — Inbox carousel and playback

- **Goal:** Signed-in message timeline and playback on the P2 inbox screen.
- **Deliverables:** Snapshot apply by message id; oldest-left order; last-viewed writes that do not steal the live selection; Play/Pause in the header; sketch replay at 120×120 with portrait hidden while ink is visible; connectivity status replaces the lower slot and pauses audio; no cached play while unreachable. Level 1 for new mail and badges.
- **Out of scope:** Recipient picker, send upload, Volume slider, device-wide archive.
- **Touchpoints:** Carousel spec, inbox contract, `COMP-MESSAGE-CARD`, `LAB-02` and `LAB-03` as reference.

#### P7 — Record canvas in the product shell

- **Goal:** Move decided capture geometry into the product shell without the lab’s terminal screen.
- **Deliverables:** 240×240 canvas, 80×240 rail, logical 120×120 points with per-point color, background color as the only erase, 3-minute cap, mic-failure and ink-overflow behavior from the send spec. No timer chrome.
- **Out of scope:** Recipient list, HTTP upload, `LAB-10` qualification strings as product copy.
- **Touchpoints:** Sketch spec, `SCR-RECORD-SKETCH`, send `EXP-011`–`EXP-022` and `EXP-037`–`EXP-039`, `LAB-10` as evidence.

#### P8 — Send, receipt, and queue

- **Goal:** One frozen capture reaches one truthful terminal result.
- **Deliverables:** Picker with focus distinct from selection; no Everyone card; empty-recipient copy; automatic submit when ready; queue across `no Wi-Fi`, HTTP 5xx, and expired session; discard on definite refusal; 2-second `sent`; return to the previous inbox selection; sender inbox does not gain a card; mute during capture discards, mute after finish keeps the queue and does not upload until the same user is signed in again.
- **Out of scope:** Changing refusal classes, profile fields, playback implementation.
- **Touchpoints:** Send spec, delivery contract, P2 send screens, P7 capture buffer.

#### P9 — Volume

- **Goal:** Desk volume as specified, and nothing else from the old settings hub.
- **Deliverables:** BOOT from inbox opens Volume and pauses playback; slider stops are silent then 78–100 step 2; speaker tracks the slider now; BOOT or mute persists; persist failure still leaves; first-run stop is 100; red record is ignored; connectivity loss leaves the slider up.
- **Out of scope:** Logout button, avatar picker, accent editor, codec number on screen, `LAB-12`.
- **Touchpoints:** [settings.md](../../product/experiences/settings.md), carousel `EXP-036`.

#### P10 — Composed journey

- **Goal:** Run the decided stages in one product image and one P1 fixture.
- **Deliverables:** Boot muted and unmuted, roster, PIN, inbox, play, record/send, Volume, mute. Overlaps: roster refresh during PIN, inbox hint during playback, send while the inbox changes, mute during media fetch. Evidence note separates L0/L1 from device L2/L3.
- **Out of scope:** New product behavior, profile editing, performance budgets promoted into [FOUNDATION-LVGL-BUDGET](../../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md) without a measured run.
- **Touchpoints:** [v2-experience-demo-roadmap.md](../../labs/v2-experience-demo-roadmap.md) `LAB-13` scenario list, used as a test list.

### Verifier briefs

#### P1 — verification

- **Must prove:** Each previously open wire row names an accepted choice. Fixture requests reject unknown schemas. PIN fixtures do not log PINs. No experience `EXP-*` row changes meaning.
- **Commands / probes:** Contract diff review. Fixture tests for health, roster, PIN replay, inbox snapshot, and send refusal-versus-wait. No device flash required.
- **Regression watch:** Lab servers for `LAB-02`, `LAB-03`, and `LAB-07` keep their current fixture revisions. P1 does not silently retarget those labs.

#### P2 — verification

- **Must prove:** New screens cite the experience rows for copy and slot composition. Catalog scenes render in `LAB-14`. Volume has no numeric readout. Send states cover queued wait and definite refusal. Inbox hides portrait during sketch ink.
- **Commands / probes:** `make lab.lab14` on a registered kit, or the catalog render tests if the lab README names a host check. Screenshot each new scene.
- **Regression watch:** Existing login and record catalog scenes still mount.

#### P3 — verification

- **Must prove:** Repeated stage entry does not grow widget or timer counts. A data event does not destroy the shell. A callback after unmount is stale. Product boot is no longer only the bootstrap log.
- **Commands / probes:** `LAB-06` model tests and `make lab.lab06.scripted` still pass. New shell tests at L1. Device L2 only for the product boot line.
- **Regression watch:** `LAB-06` markers and evidence runs stay valid.

#### P4 — verification

- **Must prove:** The `EXP-LOGIN` clusters in the traceability table, including eight users, empty hangout, cooldown through mute, PIN key replay, and health while muted. Selectable cards never appear on a malformed roster.
- **Commands / probes:** L1 reducer tests with a virtual clock for 60-second and 5-minute polls and the 1/2/4/30 connecting waits. L2 against the P1 fixture. Mask PIN in logs.
- **Regression watch:** Do not treat a `LAB-07` PASS as a product PASS. Login must not fetch an inbox.

#### P5 — verification

- **Must prove:** Actor comes from the credential. A late A success after mute or after sign-in as B does not change stage, focus, or media. Invalidate does not block mute.
- **Commands / probes:** L1 race tests. L2 fixture that returns a PIN success after mute and an inbox snapshot for the previous user.
- **Regression watch:** P4 cooldown and roster tests still pass.

#### P6 — verification

- **Must prove:** Carousel rows except BOOT (P9) and red record (P8). New mail does not move selection. Outage does not play cached audio. Sketch face hides portrait. One terminal outcome per play attempt.
- **Commands / probes:** L1 snapshot and last-viewed tests. L2 with the P1 media fixture, including the 8/16/64/256 sizes where the fixture supports them. Audio audibility claims need L3 and stay unclaimed if the run has no acoustic evidence.
- **Regression watch:** `LAB-02` and `LAB-03` fixtures and reports stay on their lab revisions.

#### P7 — verification

- **Must prove:** Logical 120×120 storage, 2× record mapping, six-swatch rail, no timer chrome, stroke cap and mic-failure behavior from the send spec.
- **Commands / probes:** L1 coordinate and color tests. L2 scripted capture. Compare a replay image with the card face once P6 exists; until then, compare against a host raster.
- **Regression watch:** `LAB-10` scripted qualification still passes unchanged.

#### P8 — verification

- **Must prove:** One client message id, one server message, recipients only, queue versus discard per `COM-013`, return to the prior inbox card, mute rules before and after finish.
- **Commands / probes:** L1 operation tests for lost receipt, duplicate id with different bytes, offline queue, and cancel during wipe. L2 fixture. No media payloads in traces.
- **Regression watch:** P6 selection and P7 capture tests. Sender inbox count does not increase.

#### P9 — verification

- **Must prove:** All seventeen Volume rows. Playback pauses on entry and play head remains for this session. Mute from Volume signs out. Red is ignored. Failed persist still leaves.
- **Commands / probes:** L1 stop table and persist-failure tests. L2 BOOT from inbox and back. Speaker-follows-slider needs L3 on device.
- **Regression watch:** Inbox selection and play head survive the round trip. No settings-hub controls appear.

#### P10 — verification

- **Must prove:** The composed path and the overlap list under P10 deliverables. Each failure mode still has one terminal outcome. Resource peaks are reported as measured or unmeasured, matching [FOUNDATION-LVGL-BUDGET](../../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md).
- **Commands / probes:** One scripted journey on the P1 fixture. Device run only with a registered kit; otherwise report L2 waiver. Do not cite lab PASS markers as this package’s result.
- **Regression watch:** P4 through P9 scenario ids still pass on the integrated image.

---

## Suggested implementation order

**Canonical package sequence:** P1 and P2 in parallel, then P3, then P4, then P5, then P6. P7 starts after P3 and can overlap P4–P6. P8 after P5, P6, and P7. P9 after P6. P10 last.

### Build order rationale

1. P1 and P2 do not need product firmware. Screen composition is already constrained by the experience specs. Wire acceptance is the gate for any package that sends HTTP.
2. P3 before P4 so login stages mount on one shell instead of growing inside `LAB-07`.
3. P4 before P5 because the credential is issued at PIN success. P5 before P6 and P8 so inbox and send cannot invent a second session owner.
4. P7 needs the shell and the sketch spec, not a live session. P8 needs both a finished capture and the signed-in fence.
5. Volume’s entry and return are inbox behavior (`EXP-SETTINGS` `EXP-008`, carousel `EXP-036`), so the journey handoff waits for P6. The 13-stop table itself does not.
6. `LAB-12` stays off this sequence. Profile editing is outside the slice.

---

## Test and fixture resources

| Asset | Use |
|-------|-----|
| [server/labs/lab-07-readiness-roster/](../../../server/labs/lab-07-readiness-roster/server.py) | Current roster fault fixture. Reference until P1 replaces the schema. |
| [server/labs/lab-02-paging/](../../../server/labs/lab-02-paging/README.md) | Inbox summary pages for `LAB-02`. |
| [server/labs/lab-03-media/](../../../server/labs/lab-03-media/server.py) | Fixed audio and sketch bytes. |
| [docs/labs/evidence/](../../labs/evidence/README.md) | `LAB-06` and `LAB-07` software qualification. Not product AC evidence. |
| [client/labs/lab-10-record-canvas/](../../../client/labs/lab-10-record-canvas/README.md) | Capture budgets and scripted runs. |
| `make lab.lab14` | Visual review of catalog scenes after P2. |
| [Coding standards §11](../../standards/client-application-coding-standards.md#11-verification-ladder) | L0 build, L1 model, L2 scripted, L3 target. |

Device commands need a kit named in the labs registry, serial access, and the ESP-IDF toolchain. Host model tests do not prove frame time, audio, or touch. `LAB-03` and `LAB-05` performance results are still unmeasured. There is no CI path in this map that flashes hardware.

---

## Cross-cutting risks summary

| Risk | Severity | Mitigation |
|------|----------|------------|
| A lab schema becomes the product protocol | High | P1 accepts wire in the contracts. Product clients talk to that revision. Labs keep their own fixture ids. |
| Experience specs still withhold implementation authorization | High | This map stays preview until accepted. P4 and later do not start against unaccepted P1 wire. |
| Full-screen rebuild on inbox or send progress | High | P3 and P6 require Level 1 data updates. Verifiers watch scene replacement. |
| `LAB-12` or the settings hub pulled into core V2 | High | No package. P9 is Volume only. |
| Working-tree portraits treated as the catalog | Medium | Initials fallback until `FOUNDATION-PROFILE-AVATARS` lists ids. |
| PIN or media in traces | Medium | P4 and P8 verifier briefs forbid them. |
| Stale lab index sends an implementer to the wrong tree | Medium | Registry file is the host source of truth. |
| Unmeasured LVGL budgets shipped as pass | Medium | P10 reports measured and unmeasured separately. |
| Queued send stored where power loss drops it | Medium | P1 chooses the medium (delivery `COM-OPEN-003`). P8 implements that choice. Send `EXP-035` already allows loss of an unfinished capture and of a finished queue on power loss. |

---

## Related artifacts

- [Endpoint login](../../product/experiences/endpoint-login.md)
- [Message carousel and playback](../../product/experiences/message-carousel-and-playback.md)
- [Sketch record and playback](../../product/experiences/sketch-record-and-playback.md)
- [Message recording and sending](../../product/experiences/message-recording-and-sending.md)
- [Volume delivery](../../product/experiences/settings.md)
- [Endpoint health and login contract](../../product/communication-contracts/endpoint-health-and-login.md)
- [User-scoped envelope](../../product/communication-contracts/user-scoped-envelope.md)
- [Inbox and media contract](../../product/communication-contracts/inbox-and-media.md)
- [Message delivery contract](../../product/communication-contracts/message-delivery.md)
- [User profile contract](../../product/communication-contracts/user-profile.md) — card-background rule only; editing stays out
- [V2 experience demo roadmap](../../labs/v2-experience-demo-roadmap.md)
- [Plans index](../README.md)
- No `retro.md` or `spec.md` in this bundle.
