# P6 hardening notes

Operator-authorized closure of residual verifier gaps (inbox carousel host + L1).

## Gaps closed

| # | Gap | Closure |
|---|-----|---------|
| 1 | No L1 recovery fetch while connecting with cached inbox | `recovery-fetch` in `product_inbox_model_test.cpp` |
| 2 | Ordinary 60s inbox poll not asserted | `inbox-fetch-60s` |
| 3 | EXP-044 refresh failure must keep selection and count | `refresh-503` |
| 4 | Level-1 new card on the right / `last_update_level` | `level1-new-card` |
| 5 | Sketch bytes not wired through session paint | `deliver_sketch_chunk` + `sketch-session` |
| 6 | Thin L2 media prefix path | `test_p6_p1_media_prefix_bytes` — Range 206 on P1 audio/sketch |
| 7 | `begin_inbox_worker` promised on inbox workers | `fl_product_inbox/workers.cpp` delegates to login helper |

## Implementation touches

- P1 fixture server: `Accept-Ranges` on media GET; `Range` → 206 `Content-Range` (inbox contract `COM-OPEN-003`).
- `CarouselSession::deliver_sketch_chunk` buffers `flsk2`, paints via `flsk2_paint_prefix`, sets `portrait_hidden` while playing.

## Regression battery

```bash
python -m unittest scripts.tests.test_product_inbox_model scripts.tests.test_product_inbox_host \
  scripts.tests.test_product_session_model scripts.tests.test_product_session_host \
  scripts.tests.test_product_login_model scripts.tests.test_product_login_host \
  scripts.tests.test_lab_p1_host scripts.tests.test_lab02_carousel_native
```
