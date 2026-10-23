# V2 core journey — implementation map

Status: **preview** (published 2026-10-22; lab inventory refreshed 2026-10-22 for `LAB-01` through `LAB-14`; contract reconciliation the same day) — companion to the experience specifications under [docs/product/experiences/](../../product/experiences/). There is no plan-bundle `spec.md`. This preview treats those experience specifications as the normative behavior source for one slice: login, inbox and playback, record/send, and Volume. It does not grant the implementation authorization those specs withhold. Refresh with `/spec-implementation-map` after the operator accepts this boundary or after a material spec or code change.

Canonical map for this bundle. Core-journey wire **decisions** are recorded in the contract revisions below. That acceptance does not finish **P1**. P1 still owes a fixture, manifest, and conformance tests. The map is not ready for orchestration until that fixture is verified. Lab fixtures are not that fixture.

Accepted decision revisions, not fixture delivery:

| Contract | Revision | What is accepted |
|----------|----------|------------------|
| [Endpoint health and login](../../product/communication-contracts/endpoint-health-and-login.md) | 0.13 | Five login routes, 32-byte session token, HTTP map, 180-second server stale clock on request acceptance, 60-second refresh, invalidate spacing |
| [User-scoped envelope](../../product/communication-contracts/user-scoped-envelope.md) | 0.10 | Schema literals, empty refresh success, empty part acknowledgement |
| [Inbox and media](../../product/communication-contracts/inbox-and-media.md) | 0.8 | One 262144-byte snapshot, ordinary and recovery fetches, sender presentation fields, prefix playback |
| [Message delivery](../../product/communication-contracts/message-delivery.md) | 0.11 | PCM limits, `flsk2` pixel replay, 16384-byte parts, open and accepted bodies, durable accepted-ID record |
| [User profile](../../product/communication-contracts/user-profile.md) | 0.9 | Core cards read the roster and inbox projections. Profile editing routes stay outside this slice |

Slice for this pass:

- In: [endpoint login](../../product/experiences/endpoint-login.md) (`EXP-LOGIN`), [message carousel and playback](../../product/experiences/message-carousel-and-playback.md), [sketch record and playback](../../product/experiences/sketch-record-and-playback.md), [message recording and sending](../../product/experiences/message-recording-and-sending.md) (`EXP-MESSAGE-RECORD-SEND`), [Volume](../../product/experiences/settings.md) (`EXP-SETTINGS`).
- Out: profile editing, the settings hub, and `LAB-12`.
- Wire: routes, the 32-byte token, PCM, `flsk2`, 16384-byte parts, HTTP status numbers, and the 262144-byte inbox body are accepted at the revisions above. **P1** still delivers the fixture and tests for those revisions. The `LAB-02` server's `fl.inbox-page/2` name is a lab subset, not that fixture.

## Spec alignment

### Strengths (spec)

- Each in-scope experience has owner-decided behavior, acceptance rows, and a follow-on table that already separates product meaning from engineering wire.
- Login, carousel, send, and Volume name the physical controls (mute, BOOT, red record) and the handoffs between them, so stage ownership can be drawn before firmware.
- Volume’s included and excluded lists keep the settings hub and profile editing out of this delivery.
- The profile contract already decides the core-journey card rule: roster and inbox faces use `card_background_id` and do not render `profile_accent`.
- Device UI already composes the signed-out screens and the record canvas. The [lab roadmap](../../labs/v2-experience-demo-roadmap.md) matches this slice (`LAB-06` through `LAB-11`, then `LAB-13`) and already defers `LAB-12`.

### Strengths (repository)

- Product firmware exists and identifies itself. [client/device/main/app_main.c](../../../client/device/main/app_main.c) logs `V2_BOOTSTRAP_READY` and does not mount a journey.
- Lab firmware exists for `LAB-02` through `LAB-12` and `LAB-14`. `LAB-01` and `LAB-13` have no project trees. `LAB-08` and `LAB-09` call `lab07::SessionWriter` and do not add a second stage owner. `LAB-12` stays outside this slice. The [lab inventory](#lab-inventory) records each lab’s evidence limit.
- Login screens and the record screen are in the device UI catalog and can be flashed with `make lab.lab14`.
- Eight card-background fills are selected in [FOUNDATION-CARD-BACKGROUNDS](../../product/device-ui/foundations/FOUNDATION-CARD-BACKGROUNDS.md). [COMP-USER-CARD](../../product/device-ui/components/COMP-USER-CARD.md) and [COMP-MESSAGE-CARD](../../product/device-ui/components/COMP-MESSAGE-CARD.md) already point card faces at `card_background_id`.
- [Client application coding standards](../../standards/client-application-coding-standards.md) and [v2 implementation practices](../../v1-assessments/v2-implementation-practices.md) already require one session owner, typed operation outcomes, and Level 0/1/2 UI updates.

### Gaps and clarifications before build

| Topic | Present state | Spec requirement | Suggested resolution |
|-------|---------------|------------------|----------------------|
| Plan bundle | Experience specs say no implementation authorization. This file is a preview map, not an approved `spec.md`. | A map the orchestrator can staff. | Keep this file as **preview** until accepted. Acceptance of the map still does not approve wire. |
| Approved wire | Decision acceptance is recorded at login 0.13, envelope 0.10, inbox 0.8, delivery 0.11, and profile 0.9. No product fixture yet speaks that set. Lab servers keep their own fixture ids. | Login, inbox, and send behavior that depends on a request or response. | **P1** delivers the fixture, manifest, and tests. Later packages consume that fixture. They do not invent routes, and they do not treat decision acceptance as P1 complete. |
| Product server root | Active docs place lab fixtures under `server/labs/`. No product server tree is named. | A server that can issue sessions, rosters, inboxes, and send receipts for the accepted schemas. | **P1** keeps the first accepted-schema server as a versioned lab fixture. Naming a product server root is a separate decision. |
| Signed-in and send screens | [screens/MISSING.md](../../product/device-ui/screens/MISSING.md) and [panels/MISSING.md](../../product/device-ui/panels/MISSING.md) are 2026-10-11 inventories. Inbox, recipient picker, send progress/result, and Volume have no `SCR-*`. Login and record screens exist as drafts. | Carousel, send, and Volume each name a visible stage. | **P2** authors those screens from the 2026-10-21 experience specs. Treat the MISSING inventories as stale checklists. |
| Volume geometry and storage | Volume stop set, copy, and leave/save behavior are decided. Slider pixels and the persist mechanism are follow-on in the Volume spec. | `EXP-SETTINGS` `EXP-011` through `EXP-017`. | **P2** may leave slider pixels as `UI-OPEN` if the owner has not picked them. **P9** implements the 13 stops and save-on-leave without painting a codec number. |
| Avatar catalog | [FOUNDATION-PROFILE-AVATARS](../../product/device-ui/foundations/FOUNDATION-PROFILE-AVATARS.md) says artwork is deferred and LAB-14 initials are the interim display. Working-tree portrait sources are not listed in that catalog. [COMP-USER-CARD-BUST](../../product/device-ui/components/COMP-USER-CARD-BUST.md) is pending owner review. | Identity surfaces show a catalog avatar, with initials as the interim fallback. | Core-journey packages use the initials fallback. Do not promote working-tree portraits or the bust treatment until the foundation lists them. |
| `client/shared/` | [Repository layout](../../labs/repository-layout.md) projects `client/shared/` for promoted modules. That directory is absent. | Lab modules are promoted before they are composed into `client/device/`. | **P3** creates `client/shared/` when the first shell module is promoted. Labs stay disposable. |
| Lab index drift | [docs/labs/README.md](../../labs/README.md) still says only `LAB-02`, `LAB-03`, `LAB-05`, and `LAB-14` are registered, and that the other rows are planned. [config/labs.registry.yaml](../../../config/labs.registry.yaml) registers `lab02` through `lab12` and `lab14`, plus `lab05peak`. [client/labs/README.md](../../../client/labs/README.md) still says the tree is documentation-only. The evidence index has no report row for `LAB-10` or `LAB-14`. | Verifiers need the real entrypoints. | Trust the registry, each lab README, and the [lab inventory](#lab-inventory). **P3** does not re-implement labs that already exist. |
| `LAB-01` and `LAB-13` | No `client/labs` or `server/labs` tree. The evidence index lists `LAB-01` as none yet. `LAB-13` is only the roadmap’s composed-journey scenario list. | A product journey still needs one integrated image. | Do not block **P6** on an x02 baseline lab. **P10** uses the `LAB-13` scenario list without waiting for a `lab-13` project. |
| Stale `LAB-07` report sentence | [REPORT-LAB-07-v1](../../labs/evidence/lab-07/REPORT-LAB-07-v1.md) says `LAB-08` and `LAB-09` were not implemented. Both firmware trees, fixture servers, and reports now exist. | Verifiers use the current trees. | Trust [REPORT-LAB-08-v1](../../labs/evidence/lab-08/REPORT-LAB-08-v1.md) and [REPORT-LAB-09-v1](../../labs/evidence/lab-09/REPORT-LAB-09-v1.md) over that sentence. |
| Shared login session writer | `lab07::SessionWriter` is the only stage writer for `LAB-07`, `LAB-08`, and `LAB-09`. It is not in `client/shared/` or `client/device`. | One product session owner. | **P3**, **P4**, and **P5** may promote that owner. They do not add a second writer inside the labs. |
| Send `EXP-015` | `EXP-MESSAGE-RECORD-SEND` has no `EXP-015` bullet (42 acceptance rows covering `EXP-001`–`EXP-014` and `EXP-016`–`EXP-043`). | — | Do not invent `EXP-015`. |
| Profile editing | [`LAB-12`](../../../client/labs/lab-12-profile-settings/README.md) firmware and [`server/labs/lab-12-profile-settings`](../../../server/labs/lab-12-profile-settings/server.py) are registered. The visual phase is an exploratory settings prototype. Its report’s software verdict is FAIL, and no passing target run is claimed. Accent edits in that lab do not recolor the static inbox cards. Profile edit routes stay proposed. | Card rendering uses `card_background_id`. Profile save, accent palette, and the settings hub stay out of this slice. Volume is the only settings delivery. | No package promotes `LAB-12`. Roster cards read the hangout member's `card_background_id`. Inbox cards read `sender_card_background_id` on the snapshot. `LAB-09`’s profile-update probe is a late-result test, not profile editing. |

### Acceptance criteria traceability

Experience IDs reuse `EXP-001`. The experience prefix is required.

| AC | Present | Delta / work |
|----|---------|----------------|
| `EXP-LOGIN` `EXP-001` `EXP-002` `EXP-003` `EXP-027` `EXP-033` `EXP-034` `EXP-042` `EXP-044` | Mute and cooldown behavior are specified. Product firmware has no mute path. `LAB-07` probes roster mute. `LAB-08` cancels PIN locally on mute and ignores a late success. Neither lab invalidates a server session. | **P4** signed-out mute, boot latch, and cooldown survival. **P5** rejects a late session after mute and runs the accepted invalidate spacing. **P1** fixture includes that route. |
| `EXP-LOGIN` `EXP-004` `EXP-005` `EXP-011` `EXP-023` `EXP-024` `EXP-037` `EXP-038` `EXP-046` | Copy, timeouts, and lower-slot status are decided. Login screens and `PANEL-STATUS-MESSAGE` exist as drafts. Roster field validity is accepted in the login contract. | **P4** client behavior. **P1** fixture for the roster schema and HTTP map. Anti-flicker hold stays an engineering choice inside **P4**, called out in the package, not a new product rule. |
| `EXP-LOGIN` `EXP-006` `EXP-007` `EXP-008` `EXP-009` `EXP-013` `EXP-019` `EXP-020` `EXP-025` `EXP-026` `EXP-028` `EXP-032` `EXP-048` | Roster order, eight-user minimum, and empty copy are decided. `SCR-SIGN-IN-ROSTER` and `SCR-EMPTY-HANGOUT` are drafts. Initials fallback is the interim avatar. | **P4**. Eight-user layout uses the existing roster carousel. Avatars stay on the initials fallback until the foundation catalog changes. |
| `EXP-LOGIN` `EXP-014` `EXP-015` `EXP-016` `EXP-017` `EXP-018` `EXP-021` `EXP-022` `EXP-029` `EXP-030` `EXP-031` `EXP-036` `EXP-039` `EXP-040` `EXP-045` `EXP-047` `EXP-049` `EXP-052` | PIN, cooldown, unknown-user, and idempotency meanings are decided. `SCR-PIN-ENTRY` and `SCR-PIN-COOLDOWN` are drafts. [`LAB-08`](../../../client/labs/lab-08-pin-cooldown/README.md) models digits, wrong PIN, five-failure cooldown, BOOT, partial-entry idle, timeout retries, and lost-response replay on a virtual clock. It does not issue a session token. The PIN route, token bytes, and restart `operation_expired` rule are accepted. The atomic in-flight claim (`COM-OPEN-007`) is still an implementation choice inside that rule. L0/L1 only; no kit was flashed. | **P4** PIN entry and replay. **P5** issues the credential. **P1** fixture for the PIN route. |
| `EXP-LOGIN` `EXP-035` `EXP-041` `EXP-043` `EXP-051` | Health-beat meaning is decided, including mute and skipped missed beats. The server clock advances when a valid beat is accepted. The client marks the attempt successful only on a valid ack. | **P4** scheduler, and stale health does not change the login screen. **P1** fixture includes a lost health response. |
| `EXP-LOGIN` `EXP-010` `EXP-012` `EXP-050` | Session issuance without inbox content is decided. The 32-byte token is accepted. `LAB-08` stops before issuing a token. [`LAB-09`](../../../client/labs/lab-09-session-isolation/README.md) isolates late inbox, media, playback-state, and profile results across mute, sign-out, and user switch. It does not approve a token or revocation protocol. | **P4** performs the login transition. **P5** is the only owner of the signed-in credential. **P1** fixture issues that token. |
| `EXP-LOGIN` `EXP-053` `EXP-055` | Background refresh, the 3-minute bound, and credential expiry after a lost invalidate are decided. Refresh failure stays signed in. | **P5** refresh scheduler and lifetime. **P4** does not own this fence. **P10** exercises refresh during recording and Volume, and connecting that lasts until mute plus PIN. |
| `EXP-LOGIN` `EXP-054` | A missing or unrecognized avatar or card background still shows the person, with initials and `card-bg-01`. | **P4** on the sign-in roster. **P6** and **P8** use the same fallback on inbox and recipient cards. |
| Carousel `EXP-001`–`EXP-015`, `EXP-018`–`EXP-024`, `EXP-038`–`EXP-043` | Playback, order, empty copy, sketch-on-card, and header transport are decided. No unread badge (`EXP-040`). No inbox screen or message carousel panel. `LAB-02` and `LAB-03` are lab evidence. Acoustic evidence for `LAB-03` is incomplete. | **P2** inbox screen. **P6** product carousel, including prefix playback and the ordinary 60-second poll. **P1** fixture for the inbox schema. |
| Carousel `EXP-016` `EXP-017` `EXP-027`–`EXP-033` | Signed-in connectivity replaces the lower slot and disables Play. Login already defines the status panel. | **P6**, reusing the login status panel. Cached playback during outage stays forbidden. Recovery polling while connecting is in the inbox contract. |
| Carousel `EXP-044` `EXP-045` | Refresh failure stays on the inbox and shows connecting. A missing or unknown sender avatar or card background uses initials and `card-bg-01`. | **P6**. Sender presentation comes from the inbox item, including a sender absent from the hangout roster. |
| Carousel `EXP-025` `EXP-026` `EXP-036` `EXP-037` | Mute discards play heads. BOOT opens Volume and pauses audio. Red record starts send. | **P5** mute. **P9** BOOT handoff. **P8** red-record handoff. |
| `EXP-SKETCH-RECORD-PLAYBACK` `SK-001`–`SK-009`, `EXP-SKP-001`–`EXP-SKP-009` | Geometry, rail, no eraser, hidden card chrome, and independent-pixel replay are decided. `SCR-RECORD-SKETCH` exists. `LAB-10` captures locally and does not send. `LAB-04` compares formats and does not pick one. | **P7** record path, including adjacent-pixel capture. **P6** card replay (`EXP-SKP-002`, `EXP-SKP-005`, `EXP-SKP-009`). **P1** fixture uses `flsk2`. |
| `EXP-MESSAGE-RECORD-SEND` `EXP-001`–`EXP-014`, `EXP-016`–`EXP-043` | Recipient rules, capture, `preparing` / `sending` / `sent`, queue, refusal-versus-wait, and no sender self-copy are decided. No picker or send screen. No `EXP-015`. [`LAB-11`](../../../client/labs/lab-11-send-receipt/README.md) freezes a synthetic payload, uploads parts, and looks up a receipt. Success text is `condition met` under lab condition `accept-on-archive-commit`. Delivery routes and part size are accepted. Storage medium remains `COM-OPEN-003`. L0/L1 only; no kit capture. | **P2** picker and send screens. **P7** capture. **P8** send and queue, after **P6**. **P1** fixture for open, part, complete, and tombstone receipts. **P5** so a muted or switched user cannot upload. |
| `EXP-MESSAGE-RECORD-SEND` `EXP-044` `EXP-045` `EXP-046` | Refresh failure stays on the send wait. Recipient cards use the catalog fallback. Inbox fetches stop from the start of recording through a pending finished send. | **P8**, using a capture-state signal from **P7**. **P6** must not fetch during that suspension. **P10** checks the suspension after Boot. |
| `EXP-SETTINGS` `EXP-001`–`EXP-017` | Volume behavior is confirmed. No Volume screen. Hub and profile edit are excluded. | **P2** Volume screen. **P9** slider, desk persist, and inbox return. |

---

## Repository layout

Product firmware is a bootstrap. Journey behavior lives in labs and in specifications. `client/shared/` is projected and absent. There is no product server tree.

| Path | Role |
|------|------|
| [client/device/main/app_main.c](../../../client/device/main/app_main.c) | Product firmware entry. Logs `V2_BOOTSTRAP_READY`. |
| [client/labs/](../../../client/labs/README.md) | One ESP-IDF project per lab. Not the shipping app. |
| [server/labs/](../../../server/labs/README.md) | Lab fixtures only, including `lab-02-paging`, `lab-03-media`, `lab-07-readiness-roster`, `lab-08-pin-cooldown`, `lab-09-session-isolation`, `lab-11-send-receipt`, and `lab-12-profile-settings`. |
| [config/labs.registry.yaml](../../../config/labs.registry.yaml) | Host entrypoints for `make lab.*`. |
| [docs/product/experiences/](../../product/experiences/) | Normative behavior for this preview. |
| [docs/product/communication-contracts/](../../product/communication-contracts/) | Decided meaning and the accepted wire revisions in the status table. Profile editing routes stay proposed. |
| [docs/product/device-ui/](../../product/device-ui/README.md) | Visual specs and catalog. Login and record screens exist. Inbox, picker, send, and Volume do not. |
| [docs/labs/v2-experience-demo-roadmap.md](../../labs/v2-experience-demo-roadmap.md) | Lab sequence. Evidence, not product approval. |
| [docs/standards/client-application-coding-standards.md](../../standards/client-application-coding-standards.md) | Ownership, budgets, verification ladder L0–L3. |

## Lab inventory

`LAB-02` through `LAB-12` and `LAB-14` have firmware under `client/labs/`. `LAB-01` and `LAB-13` do not. Servers exist only for `LAB-02`, `LAB-03`, `LAB-07`, `LAB-08`, `LAB-09`, `LAB-11`, and `LAB-12`. None of these labs are the product app.

| ID | Present | Evidence limit | Map use |
|----|---------|----------------|---------|
| `LAB-01` | No firmware. [Evidence index](../../labs/evidence/README.md) says none yet. | x02 baseline comparison is not built. | Not a package. **P6** does not wait for it. |
| `LAB-02` | [lab-02-bounded-inbox](../../../client/labs/lab-02-bounded-inbox/README.md) and [lab-02-paging](../../../server/labs/lab-02-paging/README.md). Four card views, local or HTTP paging. | [STATUS](../../labs/evidence/lab-02/STATUS.md) (2026-10-11): host tests done, L3 and `REPORT-LAB-02-v1.md` open. | **P6** card-window reference. |
| `LAB-03` | [lab-03-media-playback](../../../client/labs/lab-03-media-playback/README.md) and [lab-03-media](../../../server/labs/lab-03-media/server.py) on port 8766. Compiles the `LAB-02` inbox model and keeps its own session. | [REPORT-LAB-03-v2](../../labs/evidence/lab-03/REPORT-LAB-03-v2.md): scripted path and a 600-second run exist. Performance INCOMPLETE. No acoustic or LCD confirmation. No product login. | **P6** playback reference. |
| `LAB-04` | [lab-04-sketch-format](../../../client/labs/lab-04-sketch-format/README.md). No server. | [REPORT-LAB-04-v1](../../labs/evidence/lab-04/REPORT-LAB-04-v1.md): L0/L1, and two 2026-10-20 captures with workflow PASS and experiment PASS. Trace loss UNKNOWN. Performance UNMEASURED. It does not choose an encoding. | **P1** sketch bytes. **P6** and **P7** must not treat a comparison arm as the wire format. |
| `LAB-05` | [lab-05-transition-proof](../../../client/labs/lab-05-transition-proof/README.md). Registry also has `lab05peak`. No server. | Lifecycle runs are under [lab-05](../../labs/evidence/lab-05/runs/20261020-scripted-run-c5962756/run-manifest.json). [Peak report](../../labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md): L0/L1, L2 captures and an L3 series on `audrey-kit`. Performance UNMEASURED. Cover-down visual and the performance budget stay open. | **P3** and **P10** overlap reference. Not a product budget. |
| `LAB-06` | [lab-06-stage-shell](../../../client/labs/lab-06-stage-shell/README.md). No server. | [REPORT-LAB-06-v1](../../labs/evidence/lab-06/REPORT-LAB-06-v1.md): L0/L1 PASS. Scripted run `20261020-scripted-run-77e3a3ce` is workflow PASS and experiment PASS. Independent device qualification is pending. Performance UNMEASURED. | **P3** shell reference. |
| `LAB-07` | [lab-07-readiness-roster](../../../client/labs/lab-07-readiness-roster/README.md) and fixture server port 8767. | [REPORT-LAB-07-v1](../../labs/evidence/lab-07/REPORT-LAB-07-v1.md): L0/L1 for the corrected path. Device confirmation of that path is pending. | **P4**. |
| `LAB-08` | [lab-08-pin-cooldown](../../../client/labs/lab-08-pin-cooldown/README.md) and fixture server port 8768. Reuses `lab07::SessionWriter`. | [REPORT-LAB-08-v1](../../labs/evidence/lab-08/REPORT-LAB-08-v1.md): L0/L1. No kit flash. No session token. | **P4**. |
| `LAB-09` | [lab-09-session-isolation](../../../client/labs/lab-09-session-isolation/README.md) and fixture server port 8769. Same session writer. | [REPORT-LAB-09-v1](../../labs/evidence/lab-09/REPORT-LAB-09-v1.md): L0/L1. Scripted kit run `20261021-scripted-run-10611ef2` is workflow PASS and experiment PASS. Mute-cancel time unmeasured. | **P5**. |
| `LAB-10` | [lab-10-record-canvas](../../../client/labs/lab-10-record-canvas/README.md). No server, send, or device audio path. | [L10-09 report](../../labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md): scripted workflow PASS, experiment UNMEASURED. No pixel, touch, or audio claim. | **P7**. |
| `LAB-11` | [lab-11-send-receipt](../../../client/labs/lab-11-send-receipt/README.md) and fixture server port 8770. | [REPORT-LAB-11-v1](../../labs/evidence/lab-11/REPORT-LAB-11-v1.md): L0/L1. No kit capture. Success text is `condition met`. | **P8**. |
| `LAB-12` | [lab-12-profile-settings](../../../client/labs/lab-12-profile-settings/README.md) and fixture server port 8771. | [REPORT-LAB-12-v1](../../labs/evidence/lab-12/REPORT-LAB-12-v1.md): visual software verdict FAIL. No passing target run. | Outside the slice. |
| `LAB-13` | No firmware or server. | Roadmap scenario list only. | **P10** test list. |
| `LAB-14` | [lab-14-device-ui-catalog](../../../client/labs/lab-14-device-ui-catalog/README.md). No server. | Runs may exist under `docs/labs/evidence/lab-14/runs/`. No qualification report. Visual review of catalog scenes, not a behavior experiment. | **P2**. |

---

## Workstreams mapped to decisions

### EXP-LOGIN — signed-out readiness, roster, PIN, health

**Present:** Behavior through `EXP-055` is owner-decided. Screens `SCR-SIGN-IN-ROSTER`, `SCR-EMPTY-HANGOUT`, `SCR-PIN-ENTRY`, and `SCR-PIN-COOLDOWN` are drafts in the catalog. [`LAB-07`](../../../client/labs/lab-07-readiness-roster/README.md) has firmware and a roster fixture server. Its corrected path is software-qualified at L0/L1; device confirmation of that path is pending, and historical 2026-10-20 captures do not prove it. [`LAB-08`](../../../client/labs/lab-08-pin-cooldown/README.md) reuses `lab07::SessionWriter` for PIN, cooldown, BOOT, and mute. It stores one result per operation key and does not issue a session token. Product firmware does not speak HTTP. Health, roster, PIN, invalidate, and refresh routes are accepted in [endpoint health and login](../../product/communication-contracts/endpoint-health-and-login.md) revision 0.13. Screen copy in both labs is exploratory. `EXP-053` and `EXP-055` belong to **P5**, not to the signed-out reducer.

**Required:** One signed-out reducer applies readiness, roster, PIN, cooldown, and mute. Selectable cards appear only after a valid roster. PIN retries use one operation key. Health continues while muted and does not change the login stage.

| Resource | Role |
|----------|------|
| [endpoint-login.md](../../product/experiences/endpoint-login.md) | Acceptance source |
| [endpoint-health-and-login.md](../../product/communication-contracts/endpoint-health-and-login.md) | Meaning decided; routes accepted at revision 0.13. Fixture is **P1** |
| [client/labs/lab-07-readiness-roster/](../../../client/labs/lab-07-readiness-roster/README.md) | Lab evidence for roster, health, and mute probes |
| [server/labs/lab-07-readiness-roster/](../../../server/labs/lab-07-readiness-roster/server.py) | Lab fixture, not the accepted product schema |
| [client/labs/lab-08-pin-cooldown/](../../../client/labs/lab-08-pin-cooldown/README.md) | Lab evidence for PIN, cooldown, and local mute cancel |
| [server/labs/lab-08-pin-cooldown/](../../../server/labs/lab-08-pin-cooldown/server.py) | PIN fixture on port 8768. One stored result per key |
| [REPORT-LAB-07-v1](../../labs/evidence/lab-07/REPORT-LAB-07-v1.md) | L0/L1 for the corrected session path. Device confirmation pending |
| [REPORT-LAB-08-v1](../../labs/evidence/lab-08/REPORT-LAB-08-v1.md) | L0/L1. No kit flash. Not the product token or invalidate proof |

**Risks:** Promoting `LAB-07` or `LAB-08` in place would bake the lab fixture schema into the product. PIN values must stay out of traces. `LAB-08` cooldown copy is not visual approval.

### Session authority — credential bound to endpoint and user

**Present:** Login `EXP-050`, `EXP-053`, and `EXP-055`, and the [user-scoped envelope](../../product/communication-contracts/user-scoped-envelope.md) revision 0.10, decide that the server derives the actor from a 32-byte credential issued at PIN success, that mute invalidates it, and that the desk refreshes it every 60 seconds for a 3-minute bound. [`LAB-09`](../../../client/labs/lab-09-session-isolation/README.md) is a signed-in probe on the same `lab07::SessionWriter`. Decided inputs are `I-mute`, `I-signout`, `I-switch`, `I-push`, and `I-expire-client`. Scripted kit run `20261021-scripted-run-10611ef2` is workflow PASS and experiment PASS. Mute-to-cancel time and `L09-05` stay unmeasured. The lab does not replace the accepted token or revocation rules. No `client/shared/`.

**Required:** Inbox, media, last-viewed, and send calls use that credential. **P5** runs the refresh scheduler and the 3-minute lifetime. A late result for user A cannot paint user B. Mute and sign-out reject in-flight work locally even when invalidate is still retrying. A lost invalidate leaves the credential alive until that bound. The desk stays signed out.

| Resource | Role |
|----------|------|
| [user-scoped-envelope.md](../../product/communication-contracts/user-scoped-envelope.md) | Accepted envelope, revision 0.10 |
| [v2-implementation-practices.md](../../v1-assessments/v2-implementation-practices.md) | One reducer; workers return typed events |
| [client/labs/lab-09-session-isolation/](../../../client/labs/lab-09-session-isolation/README.md) | Late-result probe. Not a product inbox |
| [server/labs/lab-09-session-isolation/](../../../server/labs/lab-09-session-isolation/server.py) | Session fixture on port 8769 |
| [REPORT-LAB-09-v1](../../labs/evidence/lab-09/REPORT-LAB-09-v1.md) | L0/L1 plus one scripted kit PASS. Performance unmeasured |

**Risks:** A lost invalidate leaves the server credential alive until the 3-minute bound. The desk stays signed out and does not sign in again by itself. `LAB-09` records revoke-on-mute and leave-server-session as exploratory observations. Those modes do not replace the accepted invalidate spacing.

### Carousel and sketch playback

**Present:** Carousel acceptance rows are decided, including no offline audio playback and sketch-hides-portrait. Inbox screen and inbox body panel are not authored. `LAB-02` pages a summary inbox; its L3 report is still open. `LAB-03` plays fixed fixtures on port 8766; the 600-second scripted run exists and acoustic evidence is incomplete. `LAB-04` has target captures and does not choose a sketch encoding. `LAB-01` is not available as a baseline image.

**Required:** Signed-in inbox on the split pane, stable message identity, Level 1 updates for a new card on the right and for sender name and card face. This carousel has no unread badge (`EXP-040`). Header Play/Pause, 120×120 independent-pixel sketch replay, ordinary and recovery inbox fetches, prefix playback, and refresh-failure connecting.

| Resource | Role |
|----------|------|
| [message-carousel-and-playback.md](../../product/experiences/message-carousel-and-playback.md) | Acceptance source |
| [inbox-and-media.md](../../product/communication-contracts/inbox-and-media.md) | Routes and snapshot rules accepted at revision 0.8. Fixture is **P1** |
| [client/labs/lab-02-bounded-inbox/](../../../client/labs/lab-02-bounded-inbox/README.md) | Card-window lab. L3 report open |
| [client/labs/lab-03-media-playback/](../../../client/labs/lab-03-media-playback/README.md) | Playback lab. Performance incomplete |
| [client/labs/lab-04-sketch-format/](../../../client/labs/lab-04-sketch-format/README.md) | Format comparison. Not a chosen encoding |
| [REPORT-LAB-03-v2](../../labs/evidence/lab-03/REPORT-LAB-03-v2.md) | Scripted trace. No acoustic confirmation |
| [REPORT-LAB-04-v1](../../labs/evidence/lab-04/REPORT-LAB-04-v1.md) | L0/L1 plus two target captures. Performance unmeasured |

**Risks:** A full inbox rebuild on each snapshot repeats the v1 failure mode. Device paging, if introduced for memory, still has to reach every current server message (carousel `EXP-035`).

### Record and send

**Present:** Send acceptance rows through `EXP-046` are decided, including frozen recipients, keep-versus-discard, catalog fallback, inbox-fetch suspension, and no sender copy. `SCR-RECORD-SKETCH` and `LAB-10` cover local capture. The [L10-09 report](../../labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md) records a scripted workflow PASS with experiment UNMEASURED, and it does not claim pixels, touch, or audio. Picker and send screens are not authored. [`LAB-11`](../../../client/labs/lab-11-send-receipt/README.md) has firmware, a fixture server on port 8770, and L0/L1 qualification. Both firmware variants build. There is no kit capture. The payload is a synthetic blob, not a `LAB-04` sketch. Phase text `condition met` is the lab outcome for `accept-on-archive-commit`. Delivery routes, PCM, `flsk2`, 16384-byte parts, and the accepted-ID record are in [message delivery](../../product/communication-contracts/message-delivery.md) revision 0.11. Storage medium remains `COM-OPEN-003`.

**Required:** Red from the inbox opens recipient selection. A valid selection opens the 240×240 canvas. Finish becomes one `preparing` → `sending` → `sent` sequence, or a queued wait, or `couldn't send`. One `client_message_id` does not create a second message.

| Resource | Role |
|----------|------|
| [message-recording-and-sending.md](../../product/experiences/message-recording-and-sending.md) | Acceptance source |
| [message-delivery.md](../../product/communication-contracts/message-delivery.md) | Send wire accepted at revision 0.11. Storage medium still open |
| [client/labs/lab-10-record-canvas/](../../../client/labs/lab-10-record-canvas/README.md) | Local capture lab, no network |
| [REPORT-LAB-10 L10-09](../../labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md) | Scripted lifecycle PASS. Experiment unmeasured |
| [client/labs/lab-11-send-receipt/](../../../client/labs/lab-11-send-receipt/README.md) | Frozen-payload upload and receipt lookup |
| [server/labs/lab-11-send-receipt/](../../../server/labs/lab-11-send-receipt/server.py) | Send fixture `lab11-send/v1` |
| [REPORT-LAB-11-v1](../../labs/evidence/lab-11/REPORT-LAB-11-v1.md) | L0/L1. L2 blocked. Performance unmeasured |

**Risks:** `LAB-10` screen outcomes (`Completed`, `Stopped`) and `LAB-11` text `condition met` are lab presentation, distinct from the product `preparing` / `sending` / `sent` copy. Power loss may drop a finished queue (send `EXP-035`); storage medium is still delivery `COM-OPEN-003`. `LAB-11` chunk sizes 1024 and 4096 are probes, not an accepted part size.

### Volume

**Present:** `EXP-001`–`EXP-017` are confirmed. The hub is named and excluded. No Volume screen, slider component, or desk persist module. `LAB-12` is a profile-settings prototype, not this Volume screen. Its accent preview does not recolor inbox cards, and its visual software verdict is FAIL.

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
| P3 | Promote a product stage shell | code | — | `client/shared/` (new), `client/device/`, patterns from `client/labs/lab-06-stage-shell/`. Overlap reference: `client/labs/lab-05-transition-proof/` | `LAB-06` regression stays green. Product app boots past `V2_BOOTSTRAP_READY` into an empty shell. L0/L1. `LAB-05` peak is not a product budget. |
| P4 | Signed-out login | code | P1, P3 | Product session reducer; login screens; health/roster/PIN client. Reference: `client/labs/lab-07-readiness-roster/`, `client/labs/lab-08-pin-cooldown/` | Signed-out `EXP-LOGIN` rows, including `EXP-054`. Not `EXP-053` or `EXP-055`. L1 model plus L2 scripted fixture. `LAB-07` and `LAB-08` stay labs. |
| P5 | Signed-in credential, refresh, and late-result fence | code | P1, P4 | Session module used by later calls. Reference: `client/labs/lab-09-session-isolation/` | `EXP-LOGIN` `EXP-010`, `EXP-012`, `EXP-042`, `EXP-050`, `EXP-053`, `EXP-055`. Carousel `EXP-021`, `EXP-025`. Send `EXP-032`, `EXP-033`. |
| P6 | Inbox carousel and playback | code, host-ui | P1, P2, P5 | Inbox screen; playback worker; `LAB-02`, `LAB-03`, and `LAB-04` as reference only | Carousel `EXP-001`–`EXP-045` except BOOT and red-record handoffs owned by P9 and P8. |
| P7 | Record canvas in the product shell | code | P3 | Record screen; capture module promoted from `LAB-10` behavior | `EXP-SKP-001`–`EXP-SKP-008`. Send `EXP-011`, `EXP-012`, `EXP-016`, `EXP-017`, `EXP-021`, `EXP-022`, `EXP-037`–`EXP-039`. |
| P8 | Send, receipt, and queue | code | P1, P2, P5, P6, P7 | Send screens; outbox; delivery client. Reference: `client/labs/lab-11-send-receipt/` | Send `EXP-001`–`EXP-046` except the capture rows owned by P7. Delivery `COM-013` and `COM-014`. |
| P9 | Volume | code, host-ui | P2, P6 | Volume screen; desk persist; speaker | `EXP-SETTINGS` `EXP-001`–`EXP-017`. Carousel `EXP-036`. |
| P10 | Composed journey | code, tooling | P4, P5, P6, P8, P9 | `client/device` integration; one fixture compatible with P1 | Cross-stage races in the lab roadmap’s `LAB-13` list. No `lab-13` tree exists. No new product rules. |

P7 may start once P3 exists. It does not wait for P4. P8 waits for P5, P6, and P7. P9’s inbox handoff waits for P6. The local stop table can be unit-tested earlier.

### Package briefs (for implementer agents)

#### P1 — Fixture for the accepted core-journey wire

- **Goal:** Deliver a fixture, manifest, and tests for the wire revisions in the status table. Do not reopen those choices, and do not mark this package complete because the contracts already name them.
- **Deliverables:** A fixture server and fixture manifest that speak only those schemas. Tests cover ordinary success and the failure sequences in the P1 verifier: lost health acknowledgement, accepted-ID tombstone across restart, oversized inbox then recovery without a Wi-Fi drop, `fl.send-open/2`, empty part acknowledgement, `fl.send-receipt/2`, and the three sketch replay vectors.
- **Out of scope:** Product copy, card colors, avatar art, profile editing, Volume stops, and any firmware in `client/device`. Choosing the queued-send storage medium (`COM-OPEN-003`) is still open and is not closed by this fixture.
- **Touchpoints:** Login revision 0.13, envelope revision 0.10, inbox revision 0.8, delivery revision 0.11, profile revision 0.9. Lab servers stay on their own fixture ids. The `LAB-02` `fl.inbox-page/2` name is a lab subset.

#### P2 — Author missing core screens

- **Goal:** Compose the signed-in visuals the experience specs already describe.
- **Deliverables:** `SCR-MESSAGE-INBOX` (header identity, Play/Pause, message carousel, empty and connectivity states). Recipient picker screen and panels. Send progress/result screen for `preparing`, `sending`, `sent`, `couldn't send`, `no Wi-Fi`, and `connecting`, including `+N` overflow. Volume screen: title `Volume`, slider, no numeric readout. Catalog scenes and a `LAB-14` renderer path for each new panel. `UI-OPEN` only where the experience spec still delegates (Volume slider pixels, cooldown type fit).
- **Out of scope:** Retry code, persist format, profile hub, bust portraits, and changing owner-decided copy.
- **Touchpoints:** [screens/MISSING.md](../../product/device-ui/screens/MISSING.md), [panels/MISSING.md](../../product/device-ui/panels/MISSING.md), [catalog/README.md](../../product/device-ui/catalog/README.md), carousel and send and Volume specs.

#### P3 — Promote a product stage shell

- **Goal:** Give `client/device` one session writer, cover-down transitions, and panel mount/unmount that survives repeated entry.
- **Deliverables:** `client/shared/` module for the shell; `client/device` mounts it; data updates do not rebuild the shell; destroyed panels receive no callbacks. Lab `LAB-06` stays a lab.
- **Out of scope:** Real roster, PIN, inbox, or audio. Do not fold `LAB-07` through `LAB-09` into the shell package.
- **Touchpoints:** [LAYOUT-SPLIT-PANE](../../product/device-ui/layouts/LAYOUT-SPLIT-PANE.md), [PANEL-MODULE-CONTRACT](../../product/device-ui/patterns/PANEL-MODULE-CONTRACT.md), [TRANSITION-COVER-DOWN](../../product/device-ui/patterns/TRANSITION-COVER-DOWN.md), [lab-06](../../../client/labs/lab-06-stage-shell/README.md), [LAB-06 report](../../labs/evidence/lab-06/REPORT-LAB-06-v1.md), [LAB-05 peak report](../../labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md). `lab07::SessionWriter` is a later promotion candidate for **P4** and **P5**, not a second shell.

#### P4 — Signed-out login

- **Goal:** Implement the signed-out `EXP-LOGIN` rows on the product shell against the P1 fixture.
- **Deliverables:** Readiness and roster pollers off the UI thread; in-memory recency; PIN reducer with one operation key; cooldown clock that survives mute and connectivity loss; health beats that ignore user identity; lower-slot status crossfade. Eight-user roster. Initials and `card-bg-01` when an avatar or card background is missing or unrecognized (`EXP-054`). Cards use `card_background_id`.
- **Out of scope:** Session refresh, the 3-minute credential bound, and late-result fencing (`EXP-053`, `EXP-055`, owned by **P5**). Inbox contents in the login response, profile editing, and the anti-flicker hold, which stays an engineering constant with a test.
- **Touchpoints:** Login experience, login screens, [`LAB-07`](../../../client/labs/lab-07-readiness-roster/README.md) and [`LAB-08`](../../../client/labs/lab-08-pin-cooldown/README.md) as evidence only. Promote behavior through the product reducer. Leave both labs on their fixture schemas.

#### P5 — Signed-in credential, refresh, and late-result fence

- **Goal:** One credential binds endpoint and user. Later packages call through this fence.
- **Deliverables:** Issue on PIN success. Refresh that credential every 60 seconds while signed in. Honor the 3-minute bound from the last accepted refresh, or from issuance. A failed refresh stays on the current signed-in stage. Clear on mute. Invalidate with the accepted spacing and do not block mute. If invalidate never arrives, the credential dies at the 3-minute bound. Reject stale completions. Inbox and send workers take a generation and drop mismatched results. The server health clock advances on an accepted beat; the client marks the attempt successful only on a valid ack.
- **Out of scope:** Inbox UI, upload bytes, profile patch, and automatic return to PIN or the roster. `LAB-09`’s profile-update button is a stale-result probe, not this package’s profile editor.
- **Touchpoints:** Envelope contract, login `EXP-050`, `EXP-042`, `EXP-053`, and `EXP-055`, carousel `EXP-021` and `EXP-025`, send `EXP-032` and `EXP-033`, [`LAB-09`](../../../client/labs/lab-09-session-isolation/README.md) scenarios `I-mute`, `I-signout`, `I-switch`, `I-push`, and `I-expire-client`.

#### P6 — Inbox carousel and playback

- **Goal:** Signed-in message timeline and playback on the P2 inbox screen.
- **Deliverables:** Snapshot apply by message id; oldest-left order; last-viewed writes that do not steal the live selection; Play/Pause in the header; independent-pixel sketch replay at 120×120 with portrait hidden while ink is visible; prefix playback; ordinary 60-second fetch and recovery fetch while connecting; sender face from `sender_avatar_id` and `sender_card_background_id`, with initials and `card-bg-01` as fallback (`EXP-045`); refresh failure stays on the inbox (`EXP-044`). Connectivity status replaces the lower slot and pauses audio. No cached play while unreachable. Level 1 updates add a card on the right and refresh sender name and card face. There is no unread badge.
- **Out of scope:** Recipient picker, send upload, Volume slider, device-wide archive, and a product push channel.
- **Touchpoints:** Carousel spec, inbox contract revision 0.8, `COMP-MESSAGE-CARD`, `LAB-02`, `LAB-03`, and `LAB-04` as reference. Do not wait for `LAB-01`. Do not promote a `LAB-04` comparison arm as the sketch encoding. Suspend fetches while **P7** reports recording or **P8** has a pending finished send.

#### P7 — Record canvas in the product shell

- **Goal:** Move decided capture geometry into the product shell without the lab’s terminal screen.
- **Deliverables:** 240×240 canvas, 80×240 rail, logical 120×120 points with per-point color, adjacent-pixel capture, background color as the only erase, 3-minute cap, mic-failure and ink-overflow behavior from the send spec. No timer chrome. A capture-state signal that tells **P6** and **P8** when recording is in progress. Room for one finished PCM of 5,760,000 bytes plus that send’s sketch bytes, kept when storage survives. The storage medium stays `COM-OPEN-003`.
- **Out of scope:** Recipient list, HTTP upload, `LAB-10` qualification strings as product copy.
- **Touchpoints:** Sketch spec, `SCR-RECORD-SKETCH`, send `EXP-011`–`EXP-022` and `EXP-037`–`EXP-039`, [`LAB-10`](../../../client/labs/lab-10-record-canvas/README.md) and its [L10-09 report](../../labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md). That report’s workflow PASS is not a pixel or audio PASS.

#### P8 — Send, receipt, and queue

- **Goal:** One frozen capture reaches one truthful terminal result, then returns to the inbox selection **P6** was showing.
- **Deliverables:** Picker with focus distinct from selection; no Everyone card; empty-recipient copy; recipient catalog fallback (`EXP-045`); automatic submit when ready; queue across `no Wi-Fi`, HTTP 5xx, and expired session; refresh failure stays on this wait (`EXP-044`); discard on definite refusal; 2-second `sent`; return to the previous inbox selection; sender inbox does not gain a card; mute during capture discards, mute after finish keeps the queue and does not upload until the same user is signed in again. Suspend inbox fetches from the start of recording until the finished send is accepted or discarded (`EXP-046`), using the **P7** capture-state signal and this package’s pending-send flag. An accepted-ID tombstone is the same `accepted` result.
- **Out of scope:** Changing refusal classes, profile editing, playback implementation.
- **Touchpoints:** Send spec, delivery contract revision 0.11, P2 send screens, P6 selection, P7 capture buffer, [`LAB-11`](../../../client/labs/lab-11-send-receipt/README.md) as evidence for one frozen payload, part queue overflow, and receipt lookup. Product phase copy stays `preparing`, `sending`, and `sent`. Accepted part size is 16384 bytes.

#### P9 — Volume

- **Goal:** Desk volume as specified, and nothing else from the old settings hub.
- **Deliverables:** BOOT from inbox opens Volume and pauses playback; slider stops are silent then 78–100 step 2; speaker tracks the slider now; BOOT or mute persists; persist failure still leaves; first-run stop is 100; red record is ignored; connectivity loss leaves the slider up.
- **Out of scope:** Logout button, avatar picker, accent editor, codec number on screen, and [`LAB-12`](../../../client/labs/lab-12-profile-settings/README.md). That lab’s split pane is an exploratory profile prototype, not the Volume screen.
- **Touchpoints:** [settings.md](../../product/experiences/settings.md), carousel `EXP-036`.

#### P10 — Composed journey

- **Goal:** Run the decided stages in one product image and one P1 fixture.
- **Deliverables:** Boot muted and unmuted, roster, PIN, inbox, play, record/send, Volume, mute. Overlaps: roster refresh during PIN; a polled inbox snapshot during playback; session refresh during recording and during Volume; inbox-fetch suspension after Boot while a finished send is still pending; send while the inbox changes; mute during media fetch. A test may inject a snapshot change. That injection is a simulated background event, not a product push transport. Evidence note separates L0/L1 from device L2/L3.
- **Out of scope:** New product behavior, profile editing, performance budgets promoted into [FOUNDATION-LVGL-BUDGET](../../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md) without a measured run.
- **Touchpoints:** [v2-experience-demo-roadmap.md](../../labs/v2-experience-demo-roadmap.md) `LAB-13` scenario list, used as a test list. There is no `client/labs/lab-13-*` project to extend.

### Verifier briefs

#### P1 — verification

- **Must prove:** The fixture speaks login 0.13, envelope 0.10, inbox 0.8, and delivery 0.11. Decision text alone does not pass. Fixture requests reject unknown schemas. PIN fixtures do not log PINs. No experience `EXP-*` row changes meaning. Failure sequences: lost health acknowledgement, accepted-ID tombstone across server restart, oversized inbox then a valid equal revision while Wi-Fi stays up, and the three sketch replay vectors.
- **Commands / probes:** Contract diff review. Fixture tests for health, roster, PIN replay, inbox snapshot, recovery polling, and send open, part, complete, and tombstone bodies. No device flash required.
- **Regression watch:** Lab servers for `LAB-02`, `LAB-03`, `LAB-07`, `LAB-08`, `LAB-09`, `LAB-11`, and `LAB-12` keep their current fixture revisions. P1 does not silently retarget those labs. `LAB-12` stays out of the product schema.

#### P2 — verification

- **Must prove:** New screens cite the experience rows for copy and slot composition. Catalog scenes render in `LAB-14`. Volume has no numeric readout. Send states cover queued wait and definite refusal. Inbox hides portrait during sketch ink.
- **Commands / probes:** `make lab.lab14` on a registered kit, or the catalog render tests if the lab README names a host check. Screenshot each new scene.
- **Regression watch:** Existing login and record catalog scenes still mount.

#### P3 — verification

- **Must prove:** Repeated stage entry does not grow widget or timer counts. A data event does not destroy the shell. A callback after unmount is stale. Product boot is no longer only the bootstrap log.
- **Commands / probes:** `LAB-06` model tests and `make lab.lab06.scripted` still pass. New shell tests at L1. Device L2 only for the product boot line.
- **Regression watch:** `LAB-06` markers and evidence runs stay valid. The scripted `LAB-06` PASS is not independent device qualification. `LAB-05` peak numbers stay unmeasured as a product budget.

#### P4 — verification

- **Must prove:** The signed-out `EXP-LOGIN` rows, including eight users, empty hangout, cooldown through mute, PIN key replay, health while muted, and roster fallback `EXP-054`. Selectable cards never appear on a malformed roster. Refresh and the 3-minute bound are **P5** and are not required to call P4 complete.
- **Commands / probes:** L1 reducer tests with a virtual clock for 60-second and 5-minute polls and the 1/2/4/30 connecting waits. L2 against the P1 fixture. Mask PIN in logs. `LAB-07` and `LAB-08` native model tests stay green on their own fixtures.
- **Regression watch:** A `LAB-07` or `LAB-08` L0/L1 result is not a product PASS. `LAB-08` has no device capture. Login must not fetch an inbox or issue a token before **P1**. PIN traces stay free of PIN values.

#### P5 — verification

- **Must prove:** Actor comes from the credential. Refresh runs every 60 seconds and does not change stage (`EXP-053`). A lost invalidate dies at the 3-minute bound (`EXP-055`) and does not open PIN by itself. A late A success after mute or after sign-in as B does not change stage, focus, or media. Invalidate does not block mute. A lost health response advances the server clock and leaves the login screen unchanged.
- **Commands / probes:** L1 race tests. L2 fixture that returns a PIN success after mute and an inbox snapshot for the previous user. Compare generation fencing with `LAB-09` inputs `I-mute`, `I-signout`, `I-switch`, `I-push`, and `I-expire-client`.
- **Regression watch:** P4 cooldown and roster tests still pass. `LAB-09` exploratory rows `I-confused`, `I-revoke-branches`, and `I-expire-http` do not become product rules. The profile-update probe does not pull `LAB-12` into scope.

#### P6 — verification

- **Must prove:** Carousel rows except BOOT (P9) and red record (P8), including `EXP-044` and `EXP-045`. New mail does not move selection. There is no unread badge. Outage does not play cached audio. Prefix playback can start before the full hash; a late mismatch pauses the remainder. Sketch face hides portrait and paints independent pixels. Recovery polling runs while connecting with a cached inbox. One terminal outcome per play attempt.
- **Commands / probes:** L1 snapshot and last-viewed tests. L2 with the P1 media fixture, including the 8/16/64/256 sizes where the fixture supports them. Audio audibility claims need L3 and stay unclaimed if the run has no acoustic evidence.
- **Regression watch:** `LAB-02` and `LAB-03` fixtures and reports stay on their lab revisions. `LAB-02` still has no `REPORT-LAB-02-v1.md`. `LAB-04` comparison arms stay out of the product sketch format.

#### P7 — verification

- **Must prove:** Logical 120×120 storage, 2× record mapping, six-swatch rail, adjacent-pixel capture, two separated same-color strokes, a single tap, and a fast diagonal. No timer chrome. Stroke cap and mic-failure behavior from the send spec. The capture-state signal is true only while recording. The finished-send budget covers 5,760,000 audio bytes plus the sketch.
- **Commands / probes:** L1 coordinate and color tests. L2 scripted capture. Compare a replay image with the card face once P6 exists; until then, compare against a host raster.
- **Regression watch:** `LAB-10` scripted qualification still passes unchanged. L10-09 experiment UNMEASURED is not upgraded to a pixel or audio claim.

#### P8 — verification

- **Must prove:** One client message id, one server message, recipients only, queue versus discard per `COM-013`, accepted-ID tombstone per `COM-014`, return to the prior inbox card, mute rules before and after finish, recipient fallback `EXP-045`, refresh-failure wait `EXP-044`, and inbox-fetch suspension `EXP-046`.
- **Commands / probes:** L1 operation tests for lost receipt, duplicate id with different bytes, offline queue, and cancel during wipe. L2 fixture. No media payloads in traces. `scripts/tests/test_lab11_model.py` and `scripts/tests/test_lab11_host.py` stay green for the lab fixture.
- **Regression watch:** P6 selection and P7 capture tests. Sender inbox count does not increase. Product UI does not show `LAB-11` text `condition met` or treat `accept-on-archive-commit` as the product acceptance rule. Lab chunk probes 1024 and 4096 stay lab probes. The product part size is 16384 bytes.

#### P9 — verification

- **Must prove:** All seventeen Volume rows. Playback pauses on entry and play head remains for this session. Mute from Volume signs out. Red is ignored. Failed persist still leaves.
- **Commands / probes:** L1 stop table and persist-failure tests. L2 BOOT from inbox and back. Speaker-follows-slider needs L3 on device.
- **Regression watch:** Inbox selection and play head survive the round trip. No settings-hub controls appear. `LAB-12` visual and shared-profile commands are not this package’s proof.

#### P10 — verification

- **Must prove:** The composed path and the overlap list under P10 deliverables, including refresh during recording and Volume, inbox-fetch suspension after Boot while a send is pending, and a polled snapshot during playback. A simulated snapshot change is not a push transport. Each failure mode still has one terminal outcome. Resource peaks are reported as measured or unmeasured, matching [FOUNDATION-LVGL-BUDGET](../../product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md).
- **Commands / probes:** One scripted journey on the P1 fixture. Device run only with a registered kit; otherwise report L2 waiver. Do not cite lab PASS markers as this package’s result. Do not require a `LAB-01` or `LAB-13` firmware tree.
- **Regression watch:** P4 through P9 scenario ids still pass on the integrated image.

---

## Suggested implementation order

**Canonical package sequence:** P1 and P2 in parallel, then P3, then P4, then P5, then P6. P7 starts after P3 and can overlap P4–P6. P8 after P5, P6, and P7. P9 after P6. P10 last.

### Build order rationale

1. P1 and P2 do not need product firmware. Screen composition is already constrained by the experience specs. Wire acceptance is the gate for any package that sends HTTP.
2. P3 before P4 so login stages mount on one product shell. `LAB-07`, `LAB-08`, and `LAB-09` already share `lab07::SessionWriter`. Promotion copies that owner out. It does not add a second writer inside those labs, and it does not grow product login inside `LAB-07`.
3. P4 before P5 because the credential is issued at PIN success. `LAB-08` deliberately stops before a token. P5 before P6 and P8 so inbox and send cannot invent a second session owner. `LAB-09` is the late-result reference for that fence.
4. P7 needs the shell and the sketch spec, not a live session. P8 needs both a finished capture and the signed-in fence. `LAB-11` is the send-operation reference. Its synthetic blob and `condition met` text are not the product capture or the product receipt copy.
5. Volume’s entry and return are inbox behavior (`EXP-SETTINGS` `EXP-008`, carousel `EXP-036`), so the journey handoff waits for P6. The 13-stop table itself does not.
6. `LAB-12` stays off this sequence. Its landed firmware is a profile-settings prototype with a failing visual software verdict. Profile editing is outside the slice. Volume does not absorb it.

---

## Test and fixture resources

| Asset | Use |
|-------|-----|
| [server/labs/lab-07-readiness-roster/](../../../server/labs/lab-07-readiness-roster/server.py) | Roster fault fixture, port 8767. Reference until P1 replaces the schema. |
| [server/labs/lab-08-pin-cooldown/](../../../server/labs/lab-08-pin-cooldown/server.py) | PIN fixture, port 8768. One stored result per key. |
| [server/labs/lab-09-session-isolation/](../../../server/labs/lab-09-session-isolation/server.py) | Signed-in late-result fixture, port 8769. |
| [server/labs/lab-11-send-receipt/](../../../server/labs/lab-11-send-receipt/server.py) | Send and receipt fixture `lab11-send/v1`, port 8770. |
| [server/labs/lab-02-paging/](../../../server/labs/lab-02-paging/README.md) | Inbox summary pages for `LAB-02`. |
| [server/labs/lab-03-media/](../../../server/labs/lab-03-media/server.py) | Fixed audio and sketch bytes. |
| [docs/labs/evidence/](../../labs/evidence/README.md) | Evidence index. Pair it with the [lab inventory](#lab-inventory). Lab results are not product AC evidence. |
| [LAB-02 status](../../labs/evidence/lab-02/STATUS.md) | Card-window checklist. L3 report still open. |
| [LAB-04 report](../../labs/evidence/lab-04/REPORT-LAB-04-v1.md) | Sketch-format comparison. Not a chosen encoding. |
| [LAB-05 peak report](../../labs/evidence/lab-05-peak/REPORT-LAB-05-peak-v1.md) | Transition overlap. Performance unmeasured. |
| [LAB-06 report](../../labs/evidence/lab-06/REPORT-LAB-06-v1.md) | Shell L0/L1 PASS and one scripted capture. |
| [LAB-10 L10-09 report](../../labs/evidence/lab-10/REPORT-LAB-10-L10-09-20261018.md) | Local capture lifecycle. Experiment unmeasured. |
| [client/labs/lab-12-profile-settings/](../../../client/labs/lab-12-profile-settings/README.md) | Out of slice. Profile prototype. Do not use as Volume proof. |
| `make lab.lab14` | Visual review of catalog scenes after P2. |
| [Coding standards §11](../../standards/client-application-coding-standards.md#11-verification-ladder) | L0 build, L1 model, L2 scripted, L3 target. |

Device commands need a kit named in the labs registry, serial access, and the ESP-IDF toolchain. Host model tests do not prove frame time, audio, or touch. `LAB-03` and `LAB-05` performance results are still unmeasured. There is no CI path in this map that flashes hardware.

---

## Cross-cutting risks summary

| Risk | Severity | Mitigation |
|------|----------|------------|
| A lab schema becomes the product protocol | High | P1 accepts wire in the contracts. Product clients talk to that revision. Labs keep their own fixture ids. |
| Experience specs still withhold implementation authorization | High | This map stays preview until accepted. P4 and later wait for the P1 fixture. Accepted contract revisions are not that fixture. |
| Full-screen rebuild on inbox or send progress | High | P3 and P6 require Level 1 data updates. Verifiers watch scene replacement. |
| `LAB-12` or the settings hub pulled into core V2 | High | No package. P9 is Volume only. The landed `LAB-12` visual verdict is FAIL, and its accent preview is not card rendering. |
| Lab L0/L1 treated as product login, session, or send | High | `LAB-07`, `LAB-08`, and `LAB-11` have no current device capture for the qualified path. `LAB-09` has one scripted kit PASS and unmeasured mute-cancel time. P4, P5, and P8 still need their own L1/L2 against the P1 fixture. |
| Working-tree portraits treated as the catalog | Medium | Initials fallback until `FOUNDATION-PROFILE-AVATARS` lists ids. |
| PIN or media in traces | Medium | P4 and P8 verifier briefs forbid them. |
| Stale lab index sends an implementer to the wrong tree | Medium | Registry file is the host source of truth. |
| Unmeasured LVGL budgets shipped as pass | Medium | P10 reports measured and unmeasured separately. `LAB-03`, `LAB-04`, `LAB-05`, `LAB-06`, and `LAB-10` performance claims stay unmeasured. |
| Waiting on `LAB-01` or `LAB-13` firmware | Medium | Neither tree exists. P6 proceeds from `LAB-02` and `LAB-03`. P10 writes the composed image. |
| Queued send stored where power loss drops it | Medium | The budget is one finished PCM of 5,760,000 bytes plus that send’s sketch. The medium remains delivery `COM-OPEN-003`. P7 reserves the bytes. P8 keeps the queue when storage survives. Send `EXP-035` already allows loss of an unfinished capture and of a finished queue on power loss. |

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
- [LAB-07 report](../../labs/evidence/lab-07/REPORT-LAB-07-v1.md), [LAB-08 report](../../labs/evidence/lab-08/REPORT-LAB-08-v1.md), [LAB-09 report](../../labs/evidence/lab-09/REPORT-LAB-09-v1.md), [LAB-11 report](../../labs/evidence/lab-11/REPORT-LAB-11-v1.md), [LAB-12 report](../../labs/evidence/lab-12/REPORT-LAB-12-v1.md)
- [Plans index](../README.md)
- No `retro.md` or `spec.md` in this bundle.
