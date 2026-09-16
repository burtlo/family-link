# Plan: Operational contract and retries

| Field                          | Value                                                                 |
|--------------------------------|-----------------------------------------------------------------------|
| **Doc kind**                   | `feature-plan`                                                        |
| **Owners / areas**             | Firmware (`firmware/v1/`), web twin, BOX-UI / timing contract          |
| **Status**                     | `draft`                                                               |
| **Targets**                    | Durable operational contract; close PARTIAL / CONFLICT / OPEN gaps from validation |
| **Last updated**               | 2026-09-14                                                            |
| **Supersedes / superseded by** | None — complements [`v1-isolation-remaining.md`](v1-isolation-remaining.md) (twin connecting stays there) |
| **As-built**                   | None — link to [`docs/features/`](../features/_template.md) when shipped |

## At a glance

Kids and Lynn need predictable waits, retries, and failure copy when Wi-Fi or the home server blips. Several interaction stages still lack a written operational contract, so firmware and the web twin disagree or invent behavior. This plan freezes the contract where decisions already exist, gets maintainer answers for the open rows, then implements only those decided rows — without redesigning product scope or inventing outbox UX.

| Phase | Outcome | Status |
|-------|---------|--------|
| [1 — Freeze operational contract](#phase-1--freeze-operational-contract) | `docs/OPERATIONAL-CONTRACT.md` with decided rows + explicit OPEN items | `todo` |
| [2 — Close decided small gaps](#phase-2--close-decided-small-gaps) | Login failure classes, toast duration, shared timeouts, copy alignment | `todo` |
| [3 — Signed-in offline policy](#phase-3--signed-in-offline-policy) | Ribbon + play/send rules match a maintainer-approved offline contract | `todo` |
| [4 — Outbox requirements gate](#phase-4--outbox-requirements-gate) | Written outbox acceptance criteria or explicit deferral; no invented UX | `todo` |
| [5 — Twin operational parity](#phase-5--twin-operational-parity) | Twin honors the same connect / toast / pick timings and failure copy where feasible | `todo` |

---

## Background

A validation pass scored each retry / failure stage against [`BOX-UI.md`](../BOX-UI.md), [`v1-product-spec.md`](v1-product-spec.md), [`shared/v1/timing.yaml`](../../shared/v1/timing.yaml), [`STORAGE.md`](../STORAGE.md), and as-built `firmware/v1/` + `demos/server/v1_product/web/box.js`.

**Already decided (do not reopen):** Connecting stays until the server answers or Wi-Fi fails — no timeout-to-error-screen. Network failure during PIN `checking...` → connecting, never infinite checking. Upload **discard-on-fail** is current as-built until outbox is explicitly in scope. Resolved PIN / record / timing decisions in [`v1-product-spec.md`](v1-product-spec.md) stand.

**Lacking or conflicting (this plan’s focus):**

| Gap | Validation score | Why it blocks implementers |
|-----|------------------|----------------------------|
| No single operational contract doc | — | Spec / BOX-UI / code disagree; reviewers guess |
| Login HTTP 5xx treated as `wrong pin` | CONFLICT | Spec says server/network fail → connecting |
| Firmware ignores `toast_ms` for auto-dismiss | PARTIAL | Twin honors 2500 ms; box toast sticks until repaint |
| Upload / Wi-Fi join / boot hangout timeouts not in yaml | PARTIAL | Hardcoded 20s / 25s / 15s vs shared timing |
| Offline ribbon missing `saved messages still play` | PARTIAL | BOX-UI quotes secondary copy; firmware shows `offline` only |
| Offline playback needs server blob | CONFLICT | Docs imply local play; as-built toasts `can't play right now` |
| Outbox / failed upload UX | MISSING / OPEN | [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md), [`STORAGE.md`](../STORAGE.md) — discard until decided |
| Twin connect / PIN / offline | MISSING | Isolation Phase 3 covers connecting UI; this plan extends operational parity |
| PIN-reset toast wording | PARTIAL | Spec `PIN was reset` / `ask Lynn` vs code `PIN reset - ask Lynn` |

**Related docs:** [`BOX-UI.md`](../BOX-UI.md) (Connecting, Connection confidence, copy rules), [`STORAGE.md`](../STORAGE.md), [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md), [`v1-isolation-remaining.md`](v1-isolation-remaining.md) Phase 3, [`web-firmware-parity-check`](../../.cursor/skills/web-firmware-parity-check/SKILL.md).

**Out of scope:** Reopening v1 merge media (live voice, photos, drawing). Island demos. Inventing outbox UI before Phase 4 decisions. Full LVGL carousel rewrite.

---

## Phase 1 — Freeze operational contract

**Goal.** Give implementers and reviewers one checkable matrix so “what should happen” is not rediscovered from chat.

**Deliverables**

- Add [`docs/OPERATIONAL-CONTRACT.md`](../OPERATIONAL-CONTRACT.md) with:
  - **Stage matrix** for Wi-Fi, hangout probe, connecting, PIN (local / wrong / transport / lockout), signed-in offline, pick timeout, mute gate, upload (success + discard-on-fail), playback offline, twin notes.
  - **Failure-message policy** as hard rules (calm wait vs actionable vs ask Lynn; never hostnames/ports/secrets; when clear partial PIN; toast vs persistent screen).
  - **Retry constants table** mapped to `shared/v1/timing.yaml` (and a short “not yet in yaml” list).
  - **OPEN** section listing only maintainer decisions (outbox UX, local blob play, secondary offline copy, whether non-401 HTTP → connecting).
- Cite exact copy strings from BOX-UI / as-built; mark **Spec vs as-built** where they already diverge.
- Link the new doc from [`docs/AGENTS.md`](../AGENTS.md) (one line under constraints or architecture) and from BOX-UI’s Connecting / Connection confidence sections (“operational contract”).
- Do **not** invent outbox or local-playback behavior in the decided matrix — those rows stay OPEN.

**Acceptance**

- A reviewer can score firmware against the matrix without reading the validation chat.
- OPEN items are enumerated; no silent product decisions.

**Status:** `todo`

---

## Phase 2 — Close decided small gaps

**Goal.** Align code with decisions that do not need new product invention — only clarification already implied by BOX-UI / product-spec / timing.yaml.

**Prereq:** Phase 1 OPEN list must explicitly decide (or inherit) the following before coding:

| Decision | Default if maintainer silent | Rationale |
|----------|------------------------------|-----------|
| Non-401 / non-200 login HTTP (e.g. 5xx) | → **connecting** (same as transport fail); do **not** increment PIN fail count | BOX-UI: network/server failure → connecting |
| PIN-reset toast | Prefer BOX-UI / spec wording if both exist; else keep as-built and update contract | Avoid dual strings |
| Move upload / join / default JSON timeouts into yaml | **Yes** for upload + document boot hangout 15s vs probe 2.5s | Single source; parity check |

**Deliverables**

1. **Login failure classes** in `v1_api_login_user` / `v1_auth_login_task`:
   - Wrong PIN: HTTP 401 (or explicit `ok: false` with online server) → clear entry, `wrong pin`, lockout rules unchanged.
   - Transport (`st < 0`) **or** other server failure while treating server as down → `mark_offline` → connecting; **no** fail-count bump.
   - Never leave `checking...` without a transition.
2. **Toast duration:** firmware honors `V1_UI_TOAST_MS` (auto-clear bottom ribbon toast); twin already uses it — keep parity.
3. **Timing.yaml extensions** (regenerate `make v1-timing`; `make check-v1-parity`):
   - e.g. `record.upload_timeout_ms` (today 20000 in `post_wav`).
   - e.g. `connect.wifi_join_ms` (today 25000) and/or document boot `load_hangout` 15s as intentional “first paint” vs `probe_ms`.
   - Do not change connecting’s “no timeout-to-error” rule.
4. **Copy alignment:** PIN-reset toast string matches the frozen contract; grep for divergent user-visible strings.
5. Update BOX-UI acceptance scripts only if new never-acceptable cases appear (e.g. 5xx → wrong pin).

**Acceptance**

- Matrix rows for PIN verify (transport vs wrong PIN vs lockout) score **MATCH**.
- `V1_UI_TOAST_MS` used for dismiss on firmware and twin.
- New yaml keys appear in both `v1_timing.h` and `v1_timing.js`; parity check green.
- Device or twin: forced login 5xx / hang → connecting, not `wrong pin`.

**Status:** `todo`

---

## Phase 3 — Signed-in offline policy

**Goal.** When the server drops mid-session, the carousel tells the truth about send vs play without implying capabilities the box does not have.

**Prereq — maintainer picks one policy (record in OPERATIONAL-CONTRACT OPEN → decided):**

| Option | Ribbon | Play | Send |
|--------|--------|------|------|
| **A — Honest as-built** | `offline` only | Toast `can't play right now` when blob GET fails | Block send (`can't send right now`) |
| **B — Spec ribbon + honest play** | `offline` + secondary `saved messages still play` **only if** metadata/list remains; play still needs network unless Phase 3 also adds a cache | Block send |
| **C — True local play** | Spec ribbon copy | Cache last-fetched blobs (or decode path) so play works offline; mark-read queues or skips | Block send |

Default recommendation if unblocked for v1 merge: **A**, and edit BOX-UI to drop the secondary phrase until C is scheduled — avoids lying to kids. Do not implement C in this phase unless explicitly chosen.

**Deliverables**

- Update `docs/OPERATIONAL-CONTRACT.md` + [`BOX-UI.md`](../BOX-UI.md) offline ribbon row to match the chosen option.
- Implement firmware ribbon / toast / send-block to MATCH (minimal change for A or B copy-only).
- Document mark-read / position PUT behavior when offline (fail soft; no stuck UI).
- Optional: signed-in lightweight probe or rely on WS + failed ops — state the source of truth in the contract (do not invent a second heartbeat without need).

**Acceptance**

- Stage “signed-in mid-session server loss” and “playback when offline” score **MATCH** against the frozen contract.
- No connecting screen while a session user is signed in.

**Status:** `todo`

---

## Phase 4 — Outbox requirements gate

**Goal.** Stop treating failed upload as an undefined product hole — either defer explicitly or write acceptance criteria before any outbox code.

**Deliverables**

- Facilitated decision against [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md) (“Failed outbound upload”) and [`STORAGE.md`](../STORAGE.md):
  - Keep **discard-on-fail** for v1 merge, **or**
  - Specify: persistence medium (PSRAM-only vs on-chip FAT vs USB/SD), retry trigger (Wi-Fi up / heartbeat / connecting recovery), idle copy (e.g. count-only `N not sent yet`), max queue, power-loss behavior.
- Write results into OPERATIONAL-CONTRACT Stage 9 row + OPEN resolution.
- If deferred: one sentence in OPERATIONAL-CONTRACT and OPEN-QUESTIONS that v1 ships discard-on-fail; toast `couldn't send` remains; no “not sent yet” UI.
- If accepted: **new follow-on plan** (do not expand this plan into full outbox firmware). Link it here.

**Acceptance**

- Maintainer-signed outcome recorded (defer vs follow-on plan path).
- No outbox implementation in this plan’s PRs unless a follow-on plan exists with acceptance criteria.

**Status:** `todo`

---

## Phase 5 — Twin operational parity

**Goal.** Browser twin at `/box/` is a trustworthy stand-in for operational UX checks, not only carousel chrome.

**Coordination:** Connecting screen + `V1_CONNECT_*` import is owned by [`v1-isolation-remaining.md`](v1-isolation-remaining.md) Phase 3. **Do not duplicate that work.** This phase starts after that connecting UX lands (or absorbs leftover items listed below).

**Deliverables**

- After isolation connecting UX: extend twin to cover contract stages that are still twin-MISSING:
  - Signed-out probe failure → connecting; success → roster (if not already done).
  - Offline ribbon behavior matching Phase 3 policy (even if play is stubbed).
  - Pick timeout from `V1_UI_PICK_TIMEOUT_MS`.
  - Toast copy for mute / can't-send / couldn't-send if record remains stub — document stub limits in twin README.
- Keep `make check-v1-parity` green; update [`web-firmware-parity-check`](../../.cursor/skills/web-firmware-parity-check/SKILL.md) if new constants are required on the twin.
- PIN pad parity on twin is **optional** for this plan (setup login may remain); if skipped, OPERATIONAL-CONTRACT twin row must say so explicitly.

**Acceptance**

- Twin stages listed as in-scope score PARTIAL or better; any remaining MISSING called out in the contract twin row.
- Isolation Phase 3 acceptance still passes.

**Status:** `todo`

---

## Open questions

Remove or move into OPERATIONAL-CONTRACT as each is decided.

1. **Offline play policy** — A / B / C in Phase 3?
2. **Outbox for v1** — defer discard-on-fail, or schedule follow-on plan (medium, retry, idle copy)?
3. **Boot hangout timeout** — keep 15s first load vs always `probe_ms` 2.5s?
4. **Twin PIN pad** — required for operational parity, or firmware-only with documented divergence?
5. **Signed-in server truth** — WebSocket only vs periodic `GET /v1/hangout` while carousel is up?

---

## References

- Validation source: operational contract evaluation (2026-09-14) — stage matrix PARTIAL / CONFLICT / MISSING / OPEN
- Code: `firmware/v1/v1_auth.c`, `v1_connect.c`, `v1_record.c`, `v1_carousel.c`, `v1_api.c`, `x02_main.c`
- Twin: `demos/server/v1_product/web/box.js`, `v1_timing.js`
- Timing: `shared/v1/timing.yaml`
- Docs: [`BOX-UI.md`](../BOX-UI.md), [`STORAGE.md`](../STORAGE.md), [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md), [`v1-product-spec.md`](v1-product-spec.md), [`v1-isolation-remaining.md`](v1-isolation-remaining.md)
