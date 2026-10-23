# P1 hardening notes

- **Date:** 2026-10-23
- **Package:** P1 product server (`server/device`)

## Verifier gaps addressed

- [x] **GET /v2/hangout** — host test with desk Bearer; asserts `fl.hangout-roster/2` and `fixtures/roster.json` key fields.
- [x] **PIN `operation_id` replay** — duplicate POST `/v2/session/login` with the same `operation_id` within retention; second response replays the first session token; a new `operation_id` still mints a distinct session.
- [x] **PIN not in logs** — login exercised with `BoundedRedactingLog` via `create_server(..., log_path=...)`; raw PIN digits from `fixtures/accounts.json` never appear in the captured log file.
- [x] **Recovery polling (COM-011)** — explicit test name for inbox recovery after oversized control response (not Wi‑Fi drop); follow-up GET asserts valid snapshot and fixture `inbox_revision`; stable second poll without re-triggering control.

## Deferred

- **Registry wiring** — P1 remains host-only (`scripts/tests/test_lab_p1_host.py`); not added to `config/labs.registry.yaml` (out of hardening scope; no device lab runner yet).
- **Stdout/stderr tee in tests** — log file capture is sufficient for PIN redaction proof; separate stdout capture deferred unless server starts logging outside `log_message`.

## Verification

```bash
python3 -m unittest scripts.tests.test_lab_p1_host -q
```
