# Plans backlog index

Ordered index of **remaining** work drawn from [`docs/plans/*.md`](.) compared to the approved contract [`v1-product-spec.md`](v1-product-spec.md) and the active no-storage track [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md). Plan files are **not** deleted; this file is the navigation and status layer.

**Legend — item kind**

| Kind | Meaning |
|------|---------|
| `current requirement` | Needed for the stated product/qualification track |
| `implementation task` | Bounded engineering with acceptance in a plan |
| `architectural decision` | PO/maintainer choice before or during build |
| `hardware dependency` | Blocked on BOX, card, or desk procedure |
| `experiment` | Island demo / qualification epoch, not X02 by default |
| `superseded` | Replaced by newer evidence or policy; kept for history |
| `abandoned` | Explicitly closed or not to be resumed |

**Cross-check:** [`product-03-server-accepted-message-recovery.md`](product-03-server-accepted-message-recovery.md) matches committed host evidence in [`docs/evidence/product-no-storage/03-server-recovery/README.md`](../evidence/product-no-storage/03-server-recovery/README.md) (21 host tests; no `00`–`02` / `04`–`05` evidence folders yet).

---

## Delivered evidence (not backlog tasks)

Do **not** re-queue these as open implementation work.

| Deliverable | Evidence / status | Source plans |
|-------------|-------------------|--------------|
| H38 per-card geometry binding + Stage B bounded FAT32 `io_complete` on bound **32 GB** SDHC | [`h38-32gb-20261008`](../evidence/attached-storage-qualification/h38-32gb-20261008/README.md); Lynn card [`h38-lynn-20261008`](../evidence/attached-storage-qualification/h38-lynn-20261008/README.md) | [`h38-sdhc-geometry-qualification.md`](h38-sdhc-geometry-qualification.md), [`h38-bounded-sd-filesystem.md`](h38-bounded-sd-filesystem.md) |
| H35 SDMMC discovery + H37 Stage A (32 GB track) | [`attached-storage-qualification`](../evidence/attached-storage-qualification/README.md) | [`attached-storage-qualification.md`](attached-storage-qualification.md), [`h37-sd-preservation.md`](h37-sd-preservation.md) |
| Product server disk archive + resumable PCM subset | [`03-server-recovery`](../evidence/product-no-storage/03-server-recovery/README.md) | [`product-03-server-accepted-message-recovery.md`](product-03-server-accepted-message-recovery.md) |
| H34 host message-store island | [`h34-message-store`](../evidence/h34-message-store/README.md) | [`message-store-demo.md`](message-store-demo.md), [`h34-message-store-corrections.md`](h34-message-store-corrections.md) |
| Opus island (h30/h31) + partition feasibility research | [`x02-opus-partition`](../evidence/x02-opus-partition/) | [`opus-demo.md`](opus-demo.md), [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md) |
| v1 module split (structural isolation) | Code under `firmware/v1/` | [`v1-isolation-plan.md`](v1-isolation-plan.md) (`done`) |

**Retired:** overspec **64 GB** card / `121,503,744`-sector geometry and its H37 epoch — **superseded** by 32 GB track ([`docs/AGENTS.md`](../AGENTS.md)).

---

## Product (no-storage track: product-00 … product-05)

Roadmap order: baseline → volatile lifecycle → truthful receipt → (server recovery) → idempotent receipts → RAM retry. Assessment gaps **PNS-06 … PNS-16** come from [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) and roadmap “further identified work.”

### PNS-00 — Verify device/product baseline

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Identify running firmware/host and capture PIN + short send/playback baseline before feature edits. |
| **Current state** | Draft plan; no `docs/evidence/product-no-storage/00-baseline/`. INT-014 device gate still open per [`v1-isolation-remaining.md`](v1-isolation-remaining.md). |
| **Required work** | Host smoke against isolated v1 root; device PIN script (good/wrong/connecting); dated baseline report. |
| **Dependencies** | None (blocks honest reporting on all downstream device plans). |
| **Acceptance criteria** | Per [`product-00-baseline-gate.md`](product-00-baseline-gate.md): provenance matrix, real device evidence for PIN paths, defects listed separately from hardware gaps. |
| **Source** | [`product-00-baseline-gate.md`](product-00-baseline-gate.md) |

### PNS-01 — Own one volatile outgoing message

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Stable message owner, client UUID, generation-bound workers, RAM payload reader — no mutable session/globals during send. |
| **Current state** | `v1_record.c` uses shared globals and worker `v1_state_apply`; mute/sign-out can race workers. |
| **Required work** | Introduce narrow controller + RAM adapter; adapt record worker and state queue. |
| **Dependencies** | PNS-00 baseline; auth scope freeze if PIN/connectivity breaks. |
| **Acceptance criteria** | Per [`product-01-volatile-message-lifecycle.md`](product-01-volatile-message-lifecycle.md): frozen sender/recipients, no worker-driven screen changes, `recover()` empty for RAM. |
| **Source** | [`product-01-volatile-message-lifecycle.md`](product-01-volatile-message-lifecycle.md) |

### PNS-02 — Truthful, responsive send receipt

| Field | Value |
|-------|-------|
| **Kind** | `current requirement` |
| **Goal** | Honest Finishing → Sending → Sent / failure copy; no 25s silent escape; explicit Done. |
| **Current state** | `post_wav` can report success on bad transport; 2s auto-dismiss; weak JSON validation. |
| **Required work** | Remove watchdog navigate-away; bounded owner timeout; validate full `messages` coverage per interim rules until PNS-04 receipt schema on device. |
| **Dependencies** | PNS-01, PNS-00. |
| **Acceptance criteria** | Per [`product-02-truthful-send-receipt.md`](product-02-truthful-send-receipt.md): no false Sent; animation responsive; receipt survives inbox reload failure. |
| **Source** | [`product-02-truthful-send-receipt.md`](product-02-truthful-send-receipt.md) |

### PNS-03a — Server archive (implementation)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` (**delivered on dev host**) |
| **Goal** | Accepted messages survive process restart with stable seq/IDs and on-disk media. |
| **Current state** | Implemented in `demos/server/v1_product/`; evidence README documents limits (no Windows power-loss qual; firmware still multipart). |
| **Required work** | None for core archive; treat as **reference** for PNS-04/05. |
| **Dependencies** | — |
| **Acceptance criteria** | Met per [`03-server-recovery`](../evidence/product-no-storage/03-server-recovery/README.md). |
| **Source** | [`product-03-server-accepted-message-recovery.md`](product-03-server-accepted-message-recovery.md) |

### PNS-03b — Personal-PC deployment and migration

| Field | Value |
|-------|-------|
| **Kind** | `current requirement` |
| **Goal** | Run v1 product server on the household Windows target with qualified data root and safe migration from any live memory-only instance. |
| **Current state** | Host-tested on Mac only; no live metadata export/migration performed. |
| **Required work** | Identify data path; export live metadata before cutover; staged import rehearsal; Windows filesystem qualification. |
| **Dependencies** | Owner target machine path; PNS-03a. |
| **Acceptance criteria** | Per product-03 stop conditions and evidence “Limits and deployment gates”; corrupt archive fails startup; no invented seq/sender. |
| **Source** | [`product-03-server-accepted-message-recovery.md`](product-03-server-accepted-message-recovery.md), [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md) § Server |

### PNS-04 — Idempotent delivery receipts (device + contract closure)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Same `client_message_id` → one committed message; `GET /v1/outgoing/{id}`; `family-send-receipt/1` on device; multi-target atomic fan-out. |
| **Current state** | Server: multipart `client_message_id`, dedup in archive, outgoing receipt route, chunk replay — **host-tested**. Gaps: explicit multi-recipient `to_user_ids` contract, firmware adapter, broadcast vs sequential POST elimination, full receipt validation on BOX. |
| **Required work** | Close server contract gaps if any; firmware transport uses frozen ID + receipt lookup; reject blind retry before verified. |
| **Dependencies** | PNS-03a, PNS-01. |
| **Acceptance criteria** | Per [`product-04-idempotent-delivery-receipts.md`](product-04-idempotent-delivery-receipts.md). |
| **Source** | [`product-04-idempotent-delivery-receipts.md`](product-04-idempotent-delivery-receipts.md) |

### PNS-05 — Manual RAM retry and explicit discard

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | After failure/uncertainty, Retry (same bytes/ID) or Discard; block new capture until resolved. |
| **Current state** | Failed send returns to carousel and reuses buffers; no Retry UI. |
| **Required work** | Failure card UX; offline gating; integrate PNS-04 replay/lookup. |
| **Dependencies** | PNS-01, PNS-02, PNS-04. |
| **Acceptance criteria** | Per [`product-05-ram-retry-and-discard.md`](product-05-ram-retry-and-discard.md); document power-loss loss honestly. |
| **Source** | [`product-05-ram-retry-and-discard.md`](product-05-ram-retry-and-discard.md) |

### PNS-06 — Stream upload without duplicate full buffer

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Upload from RAM payload via streaming HTTP writes so ~180s PCM is not copied twice (~5.76 MB + multipart body). |
| **Current state** | `pcm_buffer_ready` + `post_wav` second allocation can fail on long clips. |
| **Required work** | Streaming multipart from bounded reader; handle short writes per ESP-IDF client semantics. |
| **Dependencies** | PNS-01 payload reader strongly recommended. |
| **Acceptance criteria** | Send path succeeds at configured duration within measured RAM; no second full-message heap peak (assessment + device measurement). |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) (finding: duration-dependent RAM twice) |

### PNS-07 — Recording stop reasons and empty-capture honesty

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Align stop timing (silence, near-zero, trim) with user-visible outcomes; no silent upload of empty/invalid capture. |
| **Current state** | `record_take` branches and repaint clock can disagree with copy. |
| **Required work** | Classify empty/failed/cancelled vs valid; block send when appropriate. |
| **Dependencies** | PNS-02. |
| **Acceptance criteria** | Assessment failure matrix row “Mic/allocation / immediate stop.” |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

### PNS-08 — Drawing lifecycle (repaint, limits, pack)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Drawing survives repaint; enforce 30s / 2048 points with feedback; reliable FLSK1 pack on send. |
| **Current state** | `paint_record` clears canvas; sketch clamps silently. |
| **Required work** | Separate message-owned sketch state from LVGL repaint; explicit limit UX. |
| **Dependencies** | PNS-01. |
| **Acceptance criteria** | Assessment rows on drawing lifecycle. |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

### PNS-09 — Picker / recipient selection reliability

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Touch selection and repaint for recipient picker remain consistent (roadmap: “repair picker selection/repaint”). |
| **Current state** | Known UX defects in assessment; spec vs code differences on timeout/auto-start. |
| **Required work** | Fix selection/repaint; reconcile with [`v1-product-spec.md`](v1-product-spec.md) / [`BOX-UI.md`](../BOX-UI.md) per scoped plan updates. |
| **Dependencies** | PNS-00. |
| **Acceptance criteria** | Device evidence: pick → record without lost selection; document any intentional spec deltas. |
| **Source** | [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md) § Device work |

### PNS-10 — Connectivity off UI thread

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | No HTTP in paint; Wi-Fi retry must not block UI for ~25s; stale online flag refreshed. |
| **Current state** | Picker paint performs HTTP; shell retry blocks; WS errors ignored in RECORD/SEND. |
| **Required work** | Move probes to workers; reconcile connection confidence with carousel/send gates. |
| **Dependencies** | PNS-00; overlaps [`v1-isolation-remaining.md`](v1-isolation-remaining.md) state ownership. |
| **Acceptance criteria** | UI responsive during connect/send; no network in `paint_*` callbacks. |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

### PNS-11 — Bounded full-length playback (streaming)

| Field | Value |
|-------|-------|
| **Kind** | `current requirement` |
| **Goal** | Play full message length with RAM independent of duration (not ~320 KiB prefix). |
| **Current state** | `v1_api.c` / `v1_carousel.c` blob cap ~10.24s effective. |
| **Required work** | Range/stream playback state machine in product firmware (may reuse H36 learnings). |
| **Dependencies** | PNS-03a server range/stream reads; optional H36 experiment first. |
| **Acceptance criteria** | 180s (and policy max) plays completely; seek/resume uses ranges. |
| **Source** | [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md), [`long-message-experiments.md`](long-message-experiments.md) Phase 2 |

### PNS-12 — Playback and read-path error honesty

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Truncated download does not mark complete; `mark_read` follows server ack; WS refresh does not false-complete. |
| **Current state** | `playback_task` treats write-break as EOF; local read state optimistic. |
| **Required work** | Full response bounds; read/mark_read gating. |
| **Dependencies** | PNS-11. |
| **Acceptance criteria** | Assessment playback/read feedback rows. |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

### PNS-13 — Inbox parse robustness and archive navigation

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Truncated JSON does not zero inbox; firmware can reach older pages server already exposes. |
| **Current state** | Parser zeros on failure; firmware shows latest page only (server paginates). |
| **Required work** | Safer parse; UI for older messages when online. |
| **Dependencies** | PNS-03a. |
| **Acceptance criteria** | No empty inbox on partial JSON; older-page navigation matches server bounded pages. |
| **Source** | [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md), assessment |

### PNS-14 — Mute / sign-out / privacy vs in-flight send

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Privacy mute and sign-out follow explicit discard/cleanup policy; no cross-user media leakage. |
| **Current state** | Shell clears session on mute; worker may still hold globals. |
| **Required work** | Tie to PNS-01 lifetime; interim explicit discard per PNS-05 stop conditions. |
| **Dependencies** | PNS-01, PNS-05. |
| **Acceptance criteria** | Stale workers cannot paint/send another user’s draft. |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

### PNS-15 — Profile / admin persistence and feedback

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Profile saves and admin actions surface success/failure; volatile PIN-reset/admin tokens understood. |
| **Current state** | Server archive persists read/position/profile in SQLite; admin tokens volatile; some save replies ignored on device. |
| **Required work** | Device feedback for profile PUT; document admin token volatility; fix known server import bugs if still present. |
| **Dependencies** | PNS-03b for production host. |
| **Acceptance criteria** | User-visible result on profile change; assessment appearance/admin row closed. |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

### PNS-16 — Web twin outbound/send parity

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Twin exercises same send/receipt semantics as firmware for regression (not hardware proof). |
| **Current state** | `box.js` uses `tinyWavBlob` / different error surfacing. |
| **Required work** | Align outbound path with product receipt rules after PNS-02/04. |
| **Dependencies** | PNS-02, PNS-04; overlaps ISO-REM Phase 3 connecting parity. |
| **Acceptance criteria** | Twin send matrix matches firmware contract in evidence. |
| **Source** | [`product-02-truthful-send-receipt.md`](product-02-truthful-send-receipt.md), assessment |

### PNS-R — No-storage roadmap umbrella

| Field | Value |
|-------|-------|
| **Kind** | `current requirement` (index only) |
| **Goal** | Keep server vs device completion visibly separate. |
| **Current state** | Server phase host-tested; device phases draft. |
| **Required work** | Execute PNS-00 → 05 then PNS-06+ as prioritized. |
| **Dependencies** | — |
| **Acceptance criteria** | Roadmap table all rows honestly marked. |
| **Source** | [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md) |

### PNS-A — Architecture assessment (reference)

| Field | Value |
|-------|-------|
| **Kind** | `superseded` as an execution queue (content remains authoritative for gaps) |
| **Goal** | Document temporary volatile-outgoing contract vs durable long-message deferral. |
| **Current state** | Findings largely folded into product-00–05 and PNS-06–16 above. |
| **Required work** | None unless policy changes. |
| **Dependencies** | — |
| **Acceptance criteria** | — |
| **Source** | [`product-without-removable-storage-assessment.md`](product-without-removable-storage-assessment.md) |

---

## Hardware / storage follow-on (post-H38; AGENTS out-of-scope until planned)

Per [`docs/AGENTS.md`](../AGENTS.md): **out of scope until separately planned** — H32 cadence, fault matrix, near-full card, physical removal, production partition choice, durable-outbox **product** integration.

### HW-01 — Attached storage Stage C (recording cadence)

| Field | Value |
|-------|-------|
| **Kind** | `experiment` / `hardware dependency` |
| **Goal** | Opus + PCM two-second cadence on SD with zero deadline misses (H32 comparison contract). |
| **Current state** | Stage B pass only; Stage C not run. |
| **Required work** | Island fixture + controller per [`attached-storage-qualification.md`](attached-storage-qualification.md) § Stage C. |
| **Dependencies** | Bound card + BOX epoch; mandatory restore discipline. |
| **Acceptance criteria** | Stage C pass criteria in attached-storage plan. |
| **Source** | [`attached-storage-qualification.md`](attached-storage-qualification.md) |

### HW-02 — Attached storage Stage D (fault, near-full, removal)

| Field | Value |
|-------|-------|
| **Kind** | `experiment` / `hardware dependency` |
| **Goal** | Interruption/capacity campaign on disposable authorized microSD. |
| **Current state** | Not started. |
| **Required work** | Stage D per plan; no format-as-recovery. |
| **Dependencies** | HW-01 pass. |
| **Acceptance criteria** | Stage D pass/fail with restoration evidence. |
| **Source** | [`attached-storage-qualification.md`](attached-storage-qualification.md) |

### HW-03 — Audrey (and Arlo) card qualification greenfield

| Field | Value |
|-------|-------|
| **Kind** | `hardware dependency` |
| **Goal** | Per-box H35 → H37 → H38 with private epoch; no reuse of another unit’s captures. |
| **Current state** | Mazi + Lynn 32 GB H38 Stage B pass; Audrey no qual; Arlo at office. |
| **Required work** | [`three-box-usb-handoff-20261008.md`](three-box-usb-handoff-20261008.md) one-box USB procedure. |
| **Dependencies** | Operator presence; SENSOR + card. |
| **Acceptance criteria** | Evidence under `docs/evidence/attached-storage-qualification/`. |
| **Source** | [`three-box-usb-handoff-20261008.md`](three-box-usb-handoff-20261008.md), [`ATTACHED-STORAGE.md`](../hardware/ATTACHED-STORAGE.md) |

### HW-04 — On-chip H32 outbox qualification

| Field | Value |
|-------|-------|
| **Kind** | `experiment` (`abandoned` as production path until re-proven) |
| **Goal** | Qualify internal FAT/WL outbox partition for cadence + recovery. |
| **Current state** | PCM cadence failed; coalescing candidate closed at read-only gate. |
| **Required work** | Only if PO revives on-chip track — corrections in [`h32-pcm-cadence-corrections.md`](h32-pcm-cadence-corrections.md) / [`onchip-storage-qualification.md`](onchip-storage-qualification.md). |
| **Dependencies** | Not a prerequisite for no-storage online track. |
| **Acceptance criteria** | On-chip plan gates. |
| **Source** | [`onchip-storage-qualification.md`](onchip-storage-qualification.md), [`h32-onchip-mount-retry.md`](h32-onchip-mount-retry.md) |

### HW-05 — Production partition layout (factory vs dual OTA)

| Field | Value |
|-------|-------|
| **Kind** | `architectural decision` |
| **Goal** | Choose layout for Opus/product growth vs recovery model. |
| **Current state** | Research: X02+Opus fails 1.5 MiB slot; 2.125 MiB single-factory vs dual-OTA tool-validated, not device-proven. |
| **Required work** | PO decision; optional device proof after choice. |
| **Dependencies** | HW-05 blocks durable Opus product integration, not PCM no-storage track. |
| **Acceptance criteria** | Documented choice + evidence if flashed. |
| **Source** | [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md), [`attached-storage-qualification.md`](attached-storage-qualification.md) |

### HW-06 — Durable outbox island (SD-backed when qualified)

| Field | Value |
|-------|-------|
| **Kind** | `experiment` |
| **Goal** | Chunks + queue survive reboot; delete after server ack. |
| **Current state** | Draft; assumed on-chip first in plan text — **product integration explicitly out of scope** in AGENTS until planned. |
| **Required work** | [`durable-outbox-demo.md`](durable-outbox-demo.md) on qualified backend (likely SD after HW-01/02). |
| **Dependencies** | Qualified storage; H34/H36 protocol patterns. |
| **Acceptance criteria** | Demo plan phases 1–5. |
| **Source** | [`durable-outbox-demo.md`](durable-outbox-demo.md) |

### HW-07 — H38 marker buffer correction

| Field | Value |
|-------|-------|
| **Kind** | `superseded` |
| **Goal** | Fix marker buffer size for IO matrix. |
| **Current state** | Delivered as part of successful H38 Stage B epoch. |
| **Required work** | None. |
| **Dependencies** | — |
| **Acceptance criteria** | `io_complete` on 32 GB evidence. |
| **Source** | [`h38-marker-buffer-correction.md`](h38-marker-buffer-correction.md) |

### HW-08 — H38 stack/layout retry plan

| Field | Value |
|-------|-------|
| **Kind** | `superseded` / archival |
| **Goal** | Historical retry notes for H38 geometry epoch. |
| **Current state** | Superseded by completed geometry qualification doc. |
| **Required work** | None unless new card fails BIND. |
| **Dependencies** | — |
| **Acceptance criteria** | — |
| **Source** | [`h38-stack-and-layout-retry.md`](h38-stack-and-layout-retry.md) |

---

## Long message / durable (experiments, H36, continuation)

Execution **paused** for storage-first sequencing per banner on [`long-message-experiments.md`](long-message-experiments.md); product server subset partially satisfied by PNS-03a / H34.

### LM-01 — Long-message experiments Phase 1 baseline measurements

| Field | Value |
|-------|-------|
| **Kind** | `experiment` |
| **Goal** | Reproducible RAM/network/server measurements for regressions. |
| **Current state** | Todo; partition feasibility slice done. |
| **Required work** | Instrumented capture per Phase 1. |
| **Dependencies** | Device + host availability. |
| **Acceptance criteria** | Phase 1 acceptance in long-message-experiments. |
| **Source** | [`long-message-experiments.md`](long-message-experiments.md) |

### LM-02 — H36 streaming playback island

| Field | Value |
|-------|-------|
| **Kind** | `experiment` |
| **Goal** | Bounded RAM playback with ranges, ETag, checkpoints — isolated from X02. |
| **Current state** | Plan status “blocked on H35 evidence” — **H35/H38 discovery satisfied**; implementation gate is review + new epoch, not product merge. |
| **Required work** | Implement H36 fixture server + demo per plan. |
| **Dependencies** | Mandatory BOX backup/restore; feeds PNS-11. |
| **Acceptance criteria** | [`h36-streaming-playback.md`](h36-streaming-playback.md) + Phase 2 in long-message-experiments. |
| **Source** | [`h36-streaming-playback.md`](h36-streaming-playback.md), [`h36-source-contract.md`](h36-source-contract.md) |

### LM-03 — Long-message Phases 4–6, 8–9 (device storage + codecs + X02 prep)

| Field | Value |
|-------|-------|
| **Kind** | `experiment` / `hardware dependency` |
| **Goal** | On-chip or SD outbox, end-to-end codecs, FLSK2 full message, integration checklist. |
| **Current state** | Phases 3 and 7 done at island level; device phases todo/deferred. |
| **Required work** | Resume only after PO unpause and HW qualification gates. |
| **Dependencies** | HW-01–02 or HW-04; HW-05 for Opus product. |
| **Acceptance criteria** | Per-phase acceptance in long-message-experiments. |
| **Source** | [`long-message-experiments.md`](long-message-experiments.md) |

### LM-04 — Durable message continuation operator doc

| Field | Value |
|-------|-------|
| **Kind** | `superseded` (historical handoff) |
| **Goal** | Operator sequence for H32→H38 era. |
| **Current state** | Checkpoint and AGENTS priority supersede “immediate work” for H32 flash. |
| **Required work** | Use [`ATTACHED-STORAGE.md`](../hardware/ATTACHED-STORAGE.md) + attached-storage plan for current ops. |
| **Dependencies** | — |
| **Acceptance criteria** | — |
| **Source** | [`durable-message-continuation.md`](durable-message-continuation.md), [`continuation-checkpoint-2026-10-05.md`](continuation-checkpoint-2026-10-05.md) |

### LM-05 — Deferred: NVS / unqualified flash as hidden outbox

| Field | Value |
|-------|-------|
| **Kind** | `abandoned` (explicit policy) |
| **Goal** | — |
| **Current state** | Roadmap forbids until qualified storage. |
| **Required work** | None. |
| **Dependencies** | — |
| **Acceptance criteria** | — |
| **Source** | [`product-no-storage-roadmap.md`](product-no-storage-roadmap.md) § Deferred storage |

---

## v1 hardening (isolation, carousel, stability)

### V1-ISO-1 — INT-014 device gate (Phase 1)

| Field | Value |
|-------|-------|
| **Kind** | `current requirement` |
| **Goal** | Serial-proven PIN: success, wrong PIN, transport fail → connecting (never infinite `checking...`). |
| **Current state** | Code guards in `v1_auth.c`; evidence not recorded; stability checklist open. |
| **Required work** | BOX-UI PIN script + log excerpts. |
| **Dependencies** | Hardware; [`v1-auth-scope-freeze`](../../.cursor/rules/v1-auth-scope-freeze.mdc). |
| **Acceptance criteria** | [`v1-isolation-remaining.md`](v1-isolation-remaining.md) Phase 1. |
| **Source** | [`v1-isolation-remaining.md`](v1-isolation-remaining.md) |

### V1-ISO-2 — State ownership harden (Phase 2)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Workers post events only; no `v1_state_apply` from `v1_record` worker. |
| **Current state** | ~10 apply sites in `v1_record.c`; auth fail events unwired. |
| **Required work** | Post/drain pattern; CI grep script. |
| **Dependencies** | V1-ISO-1 evidence (preferred). |
| **Acceptance criteria** | Phase 2 acceptance in v1-isolation-remaining. |
| **Source** | [`v1-isolation-remaining.md`](v1-isolation-remaining.md) |

### V1-ISO-3 — Twin connecting parity (Phase 3)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | `box.js` uses `V1_CONNECT_*` and connecting screen. |
| **Current state** | Connect constants not imported in twin. |
| **Required work** | Implement connecting UX per BOX-UI. |
| **Dependencies** | `make check-v1-parity`. |
| **Acceptance criteria** | Phase 3 in v1-isolation-remaining. |
| **Source** | [`v1-isolation-remaining.md`](v1-isolation-remaining.md) |

### V1-ISO-4 — Doc + as-built cutover (Phase 4)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Docs point at `firmware/v1/*`; feature record for isolation. |
| **Current state** | Stale monolith paths in BOX-UI, DEVICE-DEMOS, carousel plan. |
| **Required work** | Path fixes + `docs/features/` record. |
| **Dependencies** | None (can parallelize). |
| **Acceptance criteria** | Phase 4 in v1-isolation-remaining. |
| **Source** | [`v1-isolation-remaining.md`](v1-isolation-remaining.md) |

### V1-OP-1 — Operational contract doc and parity

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Single [`operational-contract.md`](operational-contract.md) matrix; close CONFLICT/PARTIAL rows. |
| **Current state** | Draft plan; no consolidated contract file. |
| **Required work** | Phases 1–5 in operational-contract plan (login 5xx, toast_ms, offline ribbon, outbox deferral gate, twin). |
| **Dependencies** | V1-ISO-3 for connecting overlap; PNS-05 for outbox UX alignment. |
| **Acceptance criteria** | Per [`operational-contract.md`](operational-contract.md). |
| **Source** | [`operational-contract.md`](operational-contract.md) |

### V1-CAR-1 — Carousel UI refresh phase 2 (`/app` gallery)

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Web admin upload gallery for avatars (phase 2). |
| **Current state** | Firmware + twin profile UI done; `/app` gallery not shipped. |
| **Required work** | Phase 2 in carousel-ui-refresh. |
| **Dependencies** | v1 web admin scope. |
| **Acceptance criteria** | carousel-ui-refresh build table. |
| **Source** | [`carousel-ui-refresh.md`](carousel-ui-refresh.md) |

### V1-DEMO-1 — v1 demo set phases 2–3

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Complete demo inventory and gap demos before further glue. |
| **Current state** | Phase 4 product merge marked done; phases 2–3 todo. |
| **Required work** | Per v1-demo-set checklist. |
| **Dependencies** | Lower priority than PNS/V1-ISO unless regapping islands. |
| **Acceptance criteria** | v1-demo-set phase table. |
| **Source** | [`v1-demo-set.md`](v1-demo-set.md) |

### V1-SPEC — Approved v1 product contract (remaining merge items)

| Field | Value |
|-------|-------|
| **Kind** | `current requirement` |
| **Goal** | Full v1 spec: remote TLS/Tailscale, web admin completeness, three endpoints deployed. |
| **Current state** | x02 shell exists; remote path and household deployment not closed in plans. |
| **Required work** | Track per v1-product-spec build order items 5–6 and open deployment work. |
| **Dependencies** | PNS-03b, TLS docs. |
| **Acceptance criteria** | v1-product-spec in-scope table satisfied on hardware. |
| **Source** | [`v1-product-spec.md`](v1-product-spec.md) |

### V1-STAB — Stability synthesis P2 carousel incremental LVGL

| Field | Value |
|-------|-------|
| **Kind** | `implementation task` |
| **Goal** | Incremental LVGL updates (64 KB heap budget). |
| **Current state** | Explicitly **out of scope** for v1-isolation-remaining until Phase 1–2 pass. |
| **Required work** | Separate effort per stability-synthesis P2. |
| **Dependencies** | V1-ISO-2. |
| **Acceptance criteria** | stability-synthesis §5 P2. |
| **Source** | [`stability-work-delegation.md`](stability-work-delegation.md), [`v1-isolation-remaining.md`](v1-isolation-remaining.md) R-OQ-3 |

---

## Experiments / deferred (mazi-arlo, templates, misc.)

### EXP-MAZI — Mazi–Arlo open line island demos

| Field | Value |
|-------|-------|
| **Kind** | `experiment` (desk complete; not v1 product) |
| **Goal** | Mute-as-open, live voice, diary, draw, sketch between two kits. |
| **Current state** | All five phases `done` on desk; open questions on product vs hold-to-talk remain. |
| **Required work** | None for islands; **architectural decision** if any pattern merges into hangout product. |
| **Dependencies** | — |
| **Acceptance criteria** | Plan phases marked done. |
| **Source** | [`mazi-arlo-open-line.md`](mazi-arlo-open-line.md) |

### EXP-TEMPLATE — Plan template

| Field | Value |
|-------|-------|
| **Kind** | `superseded` as backlog item |
| **Goal** | Authoring scaffold only. |
| **Source** | [`_template.md`](_template.md) |

### EXP-STAB-DELEG — Stability delegation prompts

| Field | Value |
|-------|-------|
| **Kind** | `superseded` as work queue |
| **Goal** | Historical subagent prompts; streams A–C4 marked done. |
| **Current state** | Leftover = manual PIN verification → V1-ISO-1. |
| **Source** | [`stability-work-delegation.md`](stability-work-delegation.md) |

---

## v1 product spec — explicitly out of scope (this merge)

Not backlog unless PO expands scope: live voice (h21), live draw (h26), drawing notes as v1 merge item, photos, iPhone parent app, 4th user config, presence strip, fullscreen detail — see [`v1-product-spec.md`](v1-product-spec.md) § Out of scope.

---

## Summary for agents

| Metric | Value |
|--------|------|
| **Active backlog items** | **36** (implementation tasks + current requirements + experiments not marked superseded/abandoned/delivered; excludes PNS-03a delivered core, superseded/archival rows, LM-05, EXP-TEMPLATE, EXP-STAB-DELEG) |
| **Total indexed entries** | **44** numbered rows in sections above (includes delivered reference PNS-03a, superseded, and abandoned policy rows) |
| **Superseded / archival** | PNS-A; HW-07; HW-08; LM-04; EXP-TEMPLATE; EXP-STAB-DELEG; 64 GB geometry epoch; [`v1-isolation-plan.md`](v1-isolation-plan.md) structural work (see delivered table) |
| **Abandoned / policy forbidden** | LM-05 (unqualified NVS/flash outbox); HW-04 until re-proven |

### Architectural decisions needing PO input

1. **Production partition layout** — single 2.125 MiB factory vs dual OTA ([`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md), HW-05).
2. **Failed outbound UX policy** — [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md) still points at durable outbox direction vs **temporary** RAM retry-only track in product-05; align [`operational-contract.md`](operational-contract.md) Phase 4 with PO before inventing outbox UI.
3. **Mazi–Arlo mute-as-open vs product hold-to-talk** — [`mazi-arlo-open-line.md`](mazi-arlo-open-line.md) open questions 1–2.
4. **Live household server migration** — export/migration when switching to archive server (PNS-03b); owner call if metadata cannot be recovered losslessly.
5. **Windows + power-loss durability language** — qualify on actual deployment drive before stronger promises (PNS-03b).
6. **Resume long-message / SD Stage C–D** — prioritize vs no-storage device journey (roadmap vs AGENTS hardware follow-on).
7. **Picker/recording timing vs v1 spec** — assessment notes code/spec drift (auto-start, timeout, relock); PO confirm target UX when fixing PNS-09.
8. **Retention duration / trash grace** — [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md) § Product (server cull architecture decided; durations open).

---

## Plan file index (all `docs/plans/*.md`)

| File | Role in index |
|------|----------------|
| `README.md` | This backlog |
| `v1-product-spec.md` | Approved contract (V1-SPEC) |
| `product-no-storage-roadmap.md` | Active no-storage index (PNS-R) |
| `product-00` … `product-05` | PNS-00 … PNS-05 |
| `product-without-removable-storage-assessment.md` | PNS-06–16 source |
| `product-03-server-accepted-message-recovery.md` | PNS-03a/b |
| `v1-isolation-plan.md` | Delivered structure |
| `v1-isolation-remaining.md` | V1-ISO-1 … 4 |
| `operational-contract.md` | V1-OP-1 |
| `carousel-ui-refresh.md` | V1-CAR-1 |
| `v1-demo-set.md` | V1-DEMO-1 |
| `stability-work-delegation.md` | EXP-STAB-DELEG |
| `attached-storage-qualification.md` | HW-01, HW-02 |
| `h37-sd-preservation.md` | Delivered Stage A (see evidence) |
| `h38-*` | Delivered Stage B + superseded corrections |
| `h32-*`, `onchip-storage-qualification.md` | HW-04 |
| `long-message-experiments.md` | LM-01, LM-03 |
| `h36-streaming-playback.md`, `h36-source-contract.md` | LM-02 |
| `durable-outbox-demo.md` | HW-06 |
| `durable-message-continuation.md`, `continuation-checkpoint-2026-10-05.md` | LM-04 |
| `message-store-demo.md`, `h34-message-store-corrections.md` | Delivered (H34) |
| `opus-demo.md`, `x02-opus-partition-*.md` | Delivered research / HW-05 |
| `mazi-arlo-open-line.md` | EXP-MAZI |
| `three-box-usb-handoff-20261008.md` | HW-03 ops |
| `_template.md` | Authoring only |
