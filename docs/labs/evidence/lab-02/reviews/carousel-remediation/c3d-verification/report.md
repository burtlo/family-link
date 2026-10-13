# C3d independent verification / LAB02CAR-F006

## Disposition

F006 owner tap-to-retry and APP-O-04 initial inbox retry are **verified at L1/L2**
for mechanism in this session. **Overall F006 closure remains unverified pending L3**
(device error rendering, tap input, timing). No waiver. No demonstrated regression
in the gates below.

## Changes verified

- Removed the session pump’s automatic ~1s refetch loop for missing focused items.
- Added `lab02_session_intent_retry_page_for_index` and `lab02_session_intent_retry_initial_load` with bounded coalescing, attempt tracking, and `owner_retry_*` traces; `fetch_retry` metric counts owner-initiated retries only.
- Error card tap and inbox-unavailable status tap wire to session/provider restart (HTTP builds link `lab02_page_provider_http_retry_initial_load`; C0 supplies a no-op stub).
- Boot fetch deliveries use the admitted `op_id` (fixes duplicate delivery rejection after boot reservation changes).

## Commands and results

```bash
python3 -m unittest scripts.tests.test_lab02_model_native scripts.tests.test_lab02_architecture scripts.tests.test_lab02_carousel_native scripts.tests.test_lab02_host -v
```

All 24 tests passed (including new `test_owner_retry_policy` and extended `failed-page` C0 scenario).

## Evidence levels

| Level | Result |
|---|---|
| L1 | `test_retry_policy_native`, terminal/session/http startup regressions PASS |
| L2 | C0 eleven scenarios including owner retry on `failed-page` PASS |
| L3 | Missing — target tap, visual error copy, and timing not measured |

## Recommendation

Accept C3d mechanism at L1/L2; pause per owner instruction after this verify. Next unit: **C4** push/removal identity.
