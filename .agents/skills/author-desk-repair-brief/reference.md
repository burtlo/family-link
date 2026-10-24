# Reference — desk repair brief

## Experience → contract routing

| Primary experience | Communication contract | Device bridges (start here) |
|--------------------|------------------------|-----------------------------|
| endpoint-login | endpoint-health-and-login.md | product_login_bridge, product_login_http_worker, product_volume_bridge, product_stage_router |
| message-recording-and-sending | message-delivery.md | product_record_bridge, product_send_bridge, product_stage_router, product_hardware_buttons |
| message-carousel-and-playback | inbox-and-media.md | product_inbox_bridge, product_stage_router |

## Login / PIN symptom hints

| Operator sees | Check contract | Typical proof |
|---------------|----------------|---------------|
| `wrong pin` missing; back to roster | EXP-LOGIN PIN copy; COM-020 | 200 `wrong_pin` in telemetry |
| `checking` then roster / connecting | COM-015 409, COM-014 410 | `identity_conflict` / `operation_expired` on `pin_login` |
| Stuck connecting, mute fixes | COM-014; session reset | Serial + whether BOOT escapes |
| Same `operation_id` in many logs | COM-006, desk server `pin_records` persistence | Restart server vs new client mint |

## Record / send symptom hints

| Operator sees | Invariant / standard | Typical proof |
|---------------|----------------------|---------------|
| White screen after Red | PRES-PATH, MEM-CAP | Timer path trace, canvas allocation |
| No audio | INT-WIRE | No `product_record_push_pcm` caller |
| Host green, desk broken | CLAIM §11 | L1 vs behavior |

## Verification ladder (short)

| Level | Closes |
|-------|--------|
| Host model / `scripts/tests/test_product_*_model.py` | Session rules, classifiers — **L1** |
| `make test` / `test_device_guardrails.py` | Tooling |
| `make app:build` | Compile only |
| Desk + saved serial | Behavior (see device-target-invariants desk checklist) |

## `make test` hygiene (often unrelated to PIN)

Note in brief when failures are pre-existing: lab fixture revision, send host link
missing symbols, stale host assertion counts (e.g. `selectable_cards=2` vs 4).

## Model recommendation (operator note, optional)

| Role | Suggested model tier |
|------|----------------------|
| This skill (prompt author) | Strong — synthesis |
| client-code-review orchestrator | Strong — scope + verify |
| Narrow fixer with finding ID | Mid OK |
| Independent verifier | Strong for timer path / L3 |

## Anti-patterns in prompts

- Parallel implementers on one tree
- poc-v1 / x02 “do not implement” essays instead of EXP + COM
- Closing journey on host tests alone
- Mixing guardrail program + Wave 2 in one prompt without ordering
- “Power cycle required” without retesting mute/BOOT recovery
