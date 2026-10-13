# Independent C3d verification — LAB02CAR-F006 / APP-O-04

**Verifier:** Fresh read-only subagent pass (no repo edits during verify).  
**Date:** 2026-10-13  
**Compared to:** [c3d-verification/report.md](../c3d-verification/report.md)

## One-line disposition

**Accept** C3d mechanism at **L1/L2**. **Reject** full F006 closure at L3 (not claimed).

## Agreement with prior C3d verification report

**Agree** with the existing `c3d-verification/report.md` disposition: mechanism verified L1/L2, overall F006 remains open pending L3, no demonstrated regression in the standard unittest gates.

## Criteria (independent)

### (a) Automatic ~1s missing-item refetch removed

**Pass.** `lab02_session_pump` no longer contains a timer-driven refetch loop for missing focused items. Failed pages are tracked in `s_failed_pages`; `prefetch_for_focus` / focus admission does not re-enqueue failed pages (`enqueue_fetch` is skipped when the failed bit is set). `test_retry_policy_native` pumps 120 iterations after terminal failure with unchanged `fetch_retry`; C0 `failed-page` advances 120 ms with the same assertion. No `1000000` µs / 1 s missing-item policy remains under `components/lab02/`.

**Note (not a C3d regression):** pump still performs a separate **5 s** idle `lab02_session_intent_refresh()` for page 0 when no fetches are inflight. That is inbox refresh, not failed-page / missing-item auto-retry.

### (b) Owner tap retry — failed page and initial ERROR inbox (APP-O-04)

**Pass.**

- Failed page: `card_view.c` `on_card_clicked` on `LAB02_CARD_ERROR` calls `lab02_session_intent_retry_page_for_index`; error copy is **Message unavailable** (C0 asserts).
- Initial inbox ERROR: `carousel_controller.c` shows **inbox unavailable** when `LAB02_LOAD_ERROR`; clickable `count_lab` calls `lab02_session_intent_retry_initial_load()` then `lab02_page_provider_http_retry_initial_load()` and pumps.
- `test_retry_policy_native` covers failed-page retry (attempt 2), initial ERROR → SHELL re-arm, and successful boot completion.

### (c) Coalescing, attempt, and trace behavior

**Pass.**

- 20 ms coalescing via `s_coalesce_retry_page` / `s_coalesce_retry_until_us` in `owner_retry_page` and initial retry path.
- `s_page_attempt[]` incremented on owner retry; `request_envelope` carries `attempt` into fetch terminals.
- `fetch_retry` metric increments only when a new owner retry fetch is actually queued (not during coalesced no-ops).
- Traces: `owner_retry_page`, `owner_retry_initial` with page/attempt/op_id in extra fields.
- Double-tap coalescing: `test_retry_policy_native` asserts queue depth unchanged after two rapid `intent_retry_page_for_index(16)` calls.

### (d) Boot `op_id` delivery fix

**Pass.** Boot reservation assigns non-zero `boot_op` in `lab02_session_begin_http` and `lab02_session_intent_retry_initial_load`. `lab02_page_provider_http_start_io` copies the admitted request into `s_boot_request`; `boot_task` finishes with `req.op_id` from that envelope (not hard-coded zero). `lab02_session_http_initial_request` exposes the admitted boot request for the provider. HTTP startup native tests exercise boot admission with real `op_id` on success paths.

### (e) No blocking I/O in presentation callbacks

**Pass (mechanical gate).** `scripts.tests.test_lab02_architecture.test_presentation_does_not_wait_on_io_or_sleep` passes for `carousel_controller.c`, `card_view.c`, `card_pool.c`, `snap.c`. Presentation tap handlers only call session intents and (for initial retry) non-blocking provider restart that spawns boot work on a task; HTTP client calls remain in `page_provider_http.c` worker tasks.

## Unittest gate (repo root)

```
python3 -m unittest scripts.tests.test_lab02_model_native scripts.tests.test_lab02_architecture scripts.tests.test_lab02_carousel_native scripts.tests.test_lab02_host -v
```

| Result | Count |
|--------|------:|
| **PASS** | 24 |
| FAIL | 0 |
| Time | ~9.5 s |

Notable tests for F006: `test_owner_retry_policy`, C0 `failed-page` (11 scenarios total in `test_c0_characterization`), architecture presentation I/O guard, existing session/terminal/http startup regressions.

## Evidence ladder

| Level | Assessment |
|-------|------------|
| L1 | Owner retry policy native test, session/terminal/http startup tests — **PASS** |
| L2 | C0 headless `failed-page` (no background retry, owner retry success, traversal) — **PASS** |
| L3 | Device tap input, on-screen copy, timing — **not run; remains open** |

## F006 overall (L3)

**Remains open.** No target flash, physical tap, or visual evidence in this pass. No waiver.

## Regressions / gaps demonstrated in gates

**None** in the 24-test host suite.

**Residual gaps (not gate failures):**

- L3 closure for F006 (device).
- C0 does not simulate LVGL click events for tap wiring; L2 validates session API and C0 `intent_retry_page_for_index` after failure, not full click → HTTP path in rendered UI.
- Periodic page-0 refresh (5 s) is unchanged and separate from removed 1 s missing-item policy.

## Recommendation

Proceed per execution plan: accept C3d at L1/L2; keep F006 overall open until L3; next unit C4 unless owner reprioritizes.
