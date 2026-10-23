# P5 hardening notes

Operator-authorized closure of residual verifier gaps (session credential + worker fences).

## Gaps closed

| # | Gap | Closure |
|---|-----|---------|
| 1 | No L1 `begin_send_worker` generation fence | `send-fence-mute`, `send-fence-switch` in `product_session_model_test.cpp` |
| 2 | Health success / server clock not asserted | `health-ack` checks `health_ack_count`, `health_server_time`; invalid ack does not advance; `health-signed-out` holds stage and count |
| 3 | Late pin/scoped results without focus/generation checks | Extended `late-pin-mute`, `late-pin-b`, `lab09-scenarios` (focus_id, `generation`, `scoped_generation` on media) |
| 4 | Thin L2 credential path | `test_p5_credential_lifecycle_http` — PIN → inbox → refresh `{}` → wrong Bearer → mute clears credential + invalidate job |
| 5 | Late PIN after mute | Existing `test_p5_late_pin_after_mute_http` (HTTP login + model classify) |
| 6 | Inbox stale snapshot for prior user | L1 `inbox-stale-user`; L2 `test_p5_inbox_stale_prior_user_http` with classified login body for A |

## Implementation touches

- `parse_health_server_time` + model fields `health_ack_count()` / `health_server_time()` (store only after classify gate passes).
- Host harness modes: `host-p5-mute-credential`, `host-p5-inbox-stale`.

## Regression battery

```bash
python -m unittest scripts.tests.test_product_session_model scripts.tests.test_product_session_host \
  scripts.tests.test_product_login_model scripts.tests.test_product_login_host \
  scripts.tests.test_lab09_model scripts.tests.test_lab09_host
```
