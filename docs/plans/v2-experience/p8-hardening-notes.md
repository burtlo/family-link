# P8 hardening notes

Operator-authorized closure of residual verifier gaps (send outbox L1/L2 + LAB-11 fixture identity).

## Gaps closed

| # | Gap | Closure |
|---|-----|---------|
| 1 | `inbox_fetch_may_proceed()` stub; EXP-046 vs DEC-051 | Outbox documents DEC-051 (fetch proceeds during upload stream); L1 `inbox-fetch` asserts `has_pending_http` + `may_proceed == 1` |
| 2 | `kCom013WaitMalformed` not routed through COM-013 wait path | `apply_com013()`; L1 `malformed-wait` |
| 3 | Mute-after-finish did not block upload until same user signs in | `set_upload_blocked` on outbox; journey `on_mute_after_finish` / `on_same_user_signed_in`; L1 `mute-rules` |
| 4 | LAB-11 `restart-probe.json` byte drift vs manifest | Server fixture manifest bytes/sha256 aligned to current file (revision unchanged `lab11-send/v1`) |
| 5 | `test_lab11_model` linked `-lws2_32` on non-Windows | Gate Winsock link on `sys.platform == "win32"` (host + model) |

## Implementation touches

- `fl_product_send/outbox.cpp` — upload block gate in `prepare_http_action`; COM-013 classification helper.
- `fl_product_send/journey.cpp` — mute-held upload fence; inbox-fetch delegation comment.
- `server/labs/lab-11-send-receipt/fixtures/manifest.json` — `restart-probe.json` identity.

## Regression battery

```bash
python -m unittest scripts.tests.test_product_send_model scripts.tests.test_product_send_host \
  scripts.tests.test_lab11_model scripts.tests.test_lab11_host \
  scripts.tests.test_product_inbox_model scripts.tests.test_product_record_model
```
