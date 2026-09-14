# Plan: Finish v1 isolation hardening

| Field                          | Value                                                                 |
|--------------------------------|-----------------------------------------------------------------------|
| **Doc kind**                   | `refactor-plan`                                                       |
| **Owners / areas**             | Firmware (`firmware/v1/`), web twin, BOX-UI docs                      |
| **Status**                     | `active`                                                              |
| **Targets**                    | Close INT-014 device gate; enforce `ui_task`-only state; twin + doc parity |
| **Last updated**               | 2026-09-13                                                            |
| **Supersedes / superseded by** | Continues [`v1-isolation-plan.md`](v1-isolation-plan.md) (structural phases `done`) |
| **As-built**                   | None — link to [`docs/features/`](../features/_template.md) when shipped |

**Context:** The file-split in [`v1-isolation-plan.md`](v1-isolation-plan.md) is complete (modules under `firmware/v1/`, timing contract, CMake, shim, docs cutover). This plan is the **remaining work** so Foundry can finish the intended architecture and verification — not redo the extraction.

**Related:** [`stability-synthesis.md`](../../stability-synthesis.md) §5 P1–P3 / §8, [`BOX-UI.md`](../BOX-UI.md) acceptance scripts, [`stability-work-delegation.md`](stability-work-delegation.md) (manual PIN checkbox still open), [`.cursor/skills/device-test-after-flash`](../../.cursor/skills/device-test-after-flash/SKILL.md).

---

## At a glance

The v1 product binary is already split into modules, but workers still write screen state directly, auth-fail events are unused, the web twin never got a connecting screen, and PIN login was never proven on a real BOX-3 with serial logs. Finish those gaps so auth cannot regress the way INT-014 did.

| Phase | Outcome | Status |
|-------|---------|--------|
| [1 — Device gate INT-014](#phase-1--device-gate-int-014) | Serial-proven PIN → carousel / wrong pin / connecting | `todo` |
| [2 — State ownership harden](#phase-2--state-ownership-harden) | Workers post events only; typed fail/stale/record events | `todo` |
| [3 — Twin connecting parity](#phase-3--twin-connecting-parity) | Web twin uses probe/retry timing; connecting UX | `todo` |
| [4 — Doc + as-built cutover](#phase-4--doc--as-built-cutover) | Stale monolith paths gone; feature record; optional CI grep | `todo` |

---

## Background — what is already built

Do **not** re-extract modules or re-open maintainer decisions in [`v1-product-spec.md`](v1-product-spec.md).

| Area | As-built (2026-09-10 / verified 2026-09-13) |
|------|-----------------------------------------------|
| Layout | `firmware/v1/` — `x02_main`, `v1_state`, `v1_auth`, `v1_connect`, `v1_carousel`, `v1_record`, `v1_api`, `v1_ui_common`, generated `v1_timing.h` |
| Shim | `firmware/demos/x02_product_shell.c` — one-line flash id |
| CMake | `FAMILY_DEMO=x02_product_shell` → links all `V1_SRCS` |
| Timing | `shared/v1/timing.yaml` → `make v1-timing` / `make check-v1-parity` |
| Host | `make v1-server` → `demos/server/v1_product/`; twin at `/box/` |
| INT-014 code | `login_generation`, entry gate with `v1_ui_request_repaint()`, structured `ESP_LOGI` in `v1_auth.c` |
| Docs (partial) | `AGENTS.md`, `DEMO-MAP.md`, `v1-demo-set.md` point at `firmware/v1/` |

**Line counts (current):** `v1_carousel.c` ~1334, `v1_ui_common.c` ~983, `v1_connect.c` ~684, `v1_record.c` ~468, `x02_main.c` ~429, `v1_auth.c` ~363, `v1_api.c` ~394, `v1_state.c` ~128.

### Gaps vs isolation target (§3.2 / §6 / verification)

| Gap | Evidence | Risk |
|-----|----------|------|
| Device PIN gate never closed | [`stability-work-delegation.md`](stability-work-delegation.md) checklist last item still open; synthesis still lists INT-014 as unresolved pending evidence | Ship without proof of INT-014 |
| Workers call `v1_state_apply` | **10 sites** in `v1_record.c` (including `record_task_fn`); plan required ui_task-only apply | Race class that caused INT-014 |
| `V1_EV_AUTH_FAIL` / `V1_EV_AUTH_STALE` no-ops | `v1_state_drain` empty cases; fail path still inline in login worker | Incomplete event model |
| Residual stale gate | `v1_auth_login_task` still checks `v1_state_get() != ST_PIN \|\| s_elen != 4` at entry | Generation token alone is incomplete |
| No `connect_deferred` | Probe skipped via `v1_auth_login_active()` / `v1_state_may_probe()` only | OK if audited; document or add deferred flag |
| Twin connect constants unused | `box.js` imports snap/toast/dim/sleep from `v1_timing.js` but **not** `V1_CONNECT_*`; no connecting screen | Parity gap called out in isolation §1.3 / §6.2 |
| Stale firmware paths in docs | `BOX-UI.md` L280/319, `v1-product-spec.md`, `DEVICE-DEMOS.md`, `carousel-ui-refresh.md` still cite `x02_product_shell.c` | Agents edit the wrong file |
| No as-built feature record | Isolation plan **As-built: None** | Foundry/humans lack post-ship contract |
| R3 CI grep missing | No check that non-ui modules avoid `v1_state_apply` / direct `s_st` | Regressions land silently |

**Out of scope for this plan (do not expand):** island demos, x01 removal, incremental carousel LVGL rewrite (stability P2 #8–10) — track separately after Phase 1–2 pass. Carousel may only change if Phase 2 requires a call-site swap from `apply` → `post`.

---

## Phase 1 — Device gate INT-014

**Goal.** Prove on hardware that PIN login never sticks on `checking...`, with serial evidence — before any further state-machine edits.

**Deliverables**

- Run [BOX-UI acceptance — PIN script](../BOX-UI.md#pin-script) on a BOX-3 after `make flash DEMO=x02` (Windows COM4 or Mac `/dev/cu.usbmodem*`).
- Capture ≥20 lines of monitor during: good PIN → carousel; wrong PIN → pad; server down / Wi-Fi drop mid-check → **connecting** (not frozen pad).
- Prefer `python scripts/flash.py --demo x02 --port <PORT> --monitor-seconds 30` (or skill `device-test-after-flash`).
- Paste log excerpts into session notes or a short appendix under this plan’s as-built notes; update [`stability-synthesis.md`](../../stability-synthesis.md) INT-014 row only when evidence exists (do not mark fixed on compile alone).
- **Scope freeze:** if PIN fails, fix **only** `v1_auth` / `v1_connect` / `v1_state` — no carousel/record feature work ([`v1-auth-scope-freeze`](../../.cursor/rules/v1-auth-scope-freeze.mdc)).

**Acceptance**

- Maintainer or agent records: screen outcome + log lines showing `{event, s_st, busy, gen}` (or equivalent `pin phase` logs) for success and at least one failure path.
- Never-acceptable outcome ruled out: infinite `checking...` with `busy=0` and no transition.

**Status:** `todo`

---

## Phase 2 — State ownership harden

**Goal.** Match isolation §3.2: only `ui_task` (via `v1_state_drain` / controlled apply) mutates `s_st`; workers enqueue typed events.

**Deliverables**

1. **Ban worker `v1_state_apply`:** replace all `v1_record.c` apply sites with `v1_state_post` / `v1_state_post_goto` (or new `V1_EV_RECORD_DONE` / cancel events). Keep the intentional “apply before wake worker” race fix by posting from the LVGL handler on the UI path, or posting then draining before `give_work` inside `ui_task` — do not leave FreeRTOS workers writing `s_st`.
2. **Audit `x02_main.c`:** apply calls inside `ui_task` / `app_main` may stay, or convert to post+drain for consistency; document the rule in `v1_state.h` comment.
3. **Wire auth fail/stale:** login worker posts `V1_EV_AUTH_FAIL` / `V1_EV_AUTH_STALE`; `v1_state_drain` clears busy UI (`checking...` → dots / wrong-pin / connecting) — never silent `continue` without reconciliation ([`async-state-no-stale-gates`](../../.cursor/rules/async-state-no-stale-gates.mdc)).
4. **Reduce stale gates:** prefer generation-token match over `v1_state_get() == ST_PIN && s_elen == 4` for applying login results (stability P1 #4).
5. **Probe deferral:** confirm probe cannot call `enter_connecting` while `v1_auth_login_active()`; either keep `v1_state_may_probe()` or add explicit `connect_deferred` flag as in the original sketch — document which.
6. **Guardrail:** add a small script or CI-friendly grep (e.g. `scripts/check_v1_state_owners.py`) that fails if `v1_state_apply(` appears outside `v1_state.c` and `x02_main.c` (adjust allowlist if UI handlers need apply under lock — prefer zero outside `x02_main`).

**Acceptance**

- `rg 'v1_state_apply\(' firmware/v1` shows only allowlisted files.
- `make build-firmware DEMO=x02` green; `make check-v1-parity` green.
- Re-run Phase 1 PIN script (or twin + serial if device available) after the change.

**Status:** `todo`

---

## Phase 3 — Twin connecting parity

**Goal.** Web twin shares connect timing and signed-out connecting behavior with firmware (isolation §6.2 “future”).

**Deliverables**

- Import and use `V1_CONNECT_PROBE_MS` / `V1_CONNECT_RETRY_MS` (and wifi retry if needed) from `v1_timing.js` in `box.js`.
- Implement a minimal **connecting** screen on the twin: mailbox + `connecting` + animated dots per [`BOX-UI.md`](../BOX-UI.md#connecting-waiting-for-server); retry cadence from generated constants.
- Wire signed-out probe failure → connecting; success → roster (mirror firmware confidence rules at a high level — FreeRTOS races need not be simulated).
- Run `make check-v1-parity`; update [`web-firmware-parity-check`](../../.cursor/skills/web-firmware-parity-check/SKILL.md) if the skill still says connect constants are missing from the twin.

**Acceptance**

- Twin at `http://localhost:8080/box/` shows connecting UX when the host is unreachable (or simulated offline).
- Snap + connect constants both come from generated `v1_timing.js` (no hardcoded 2500/5000 in `box.js`).

**Status:** `todo`

---

## Phase 4 — Doc + as-built cutover

**Goal.** Agents and humans land edits in `firmware/v1/`, not the shim; isolation has a feature record.

**Deliverables**

- Update firmware references that still point at the monolith body:
  - [`BOX-UI.md`](../BOX-UI.md) (Connecting + Connection confidence “Firmware reference” lines)
  - [`v1-product-spec.md`](v1-product-spec.md)
  - [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md)
  - [`carousel-ui-refresh.md`](carousel-ui-refresh.md) if still active
- Point auth symbols at `v1_auth.c` / `v1_connect.c` (e.g. `v1_auth_login_task`, `v1_connect_enter_from_signin`).
- Create as-built under `docs/features/` (from `_template.md`) summarizing module map, build commands, and INT-014 acceptance; set **As-built** links on this plan and optionally on [`v1-isolation-plan.md`](v1-isolation-plan.md).
- Resolve isolation OQ-1 lightly: keep committing generated `v1_timing.*` + `make check-v1-parity` in PR checklist (recommend; do not build a new CI workflow unless asked).
- Leave OQ-2 (x01 deprecation) and optional shim deletion alone unless maintainer requests.

**Acceptance**

- Repo search for functional references to logic inside `x02_product_shell.c` returns only “shim / flash id” mentions.
- Feature record linked from both plans.

**Status:** `todo`

---

## Foundry kickoff (suggested)

Give Foundry this doc as the problem packet / plan source. Suggested order:

```text
Use foundry. Finish docs/plans/v1-isolation-remaining.md starting at Phase 1.
Freeze carousel edits until Phase 1 PIN device gate passes.
Follow device-test-after-flash and v1-auth-scope-freeze.
Do not reopen v1-product-spec resolved decisions.
```

If running offline local tickets, one ticket per phase is enough (e.g. `ISO-REM-001` … `004`) with this plan linked as the contract.

**Parallelism:** Phase 3 (twin) and Phase 4 (docs) may start after Phase 1 passes; Phase 2 should not merge before Phase 1 evidence (or an explicit maintainer waiver). Phase 4 doc path fixes for BOX-UI can land anytime without waiting on firmware.

---

## Open questions

Only items still open after isolation; do not reopen product PIN/caps/endpoints.

| # | Question | Default if Foundry must proceed |
|---|----------|----------------------------------|
| R-OQ-1 | Is Phase 1 blocked without hardware this session? | Yes — stop after documenting build-green + twin PIN script; do not claim INT-014 fixed |
| R-OQ-2 | Allow `v1_state_apply` in LVGL handlers under `board_lvgl_lock`, or UI-thread post only? | Prefer post-only; allowlist `x02_main.c` only if needed |
| R-OQ-3 | Incremental carousel (stability P2 #8) in this plan? | **No** — separate plan after Phase 1–2 |

---

## References

- Parent plan: [`v1-isolation-plan.md`](v1-isolation-plan.md) §3.2, §5–7
- Stability: [`stability-synthesis.md`](../../stability-synthesis.md) §5 P1, §8.1, §9
- Delegation leftover: [`stability-work-delegation.md`](stability-work-delegation.md) verification checklist
- Auth: `firmware/v1/v1_auth.c`, `v1_state.c`, `v1_record.c`
- Twin: `demos/server/v1_product/web/box.js`, `v1_timing.js`
- Spec UX: [`BOX-UI.md`](../BOX-UI.md) L284–313 + Acceptance scripts appendix
