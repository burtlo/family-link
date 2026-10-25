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
| Crash / hang at RED or ring reverse | Split: MEM-LEDGER vs PRES-PATH vs SD | Serial phase tags; mem_diag; not “pool” by default |
| No audio | INT-WIRE | No `product_record_push_pcm` caller |
| Host green, desk broken | CLAIM §11 | L1 vs behavior |

## Record / send — concurrent resources (do not single-cause)

At **RED finish**, overlapping work often includes:

1. **`finish_capture`** — encode sketch, **SD write + flush** on presentation/button path
2. **`arm_finish_collapse`** — ring reverse animation (`paint_motion`)
3. **Upload** — after collapse hold: HTTP reads same SD object (worker task)
4. **Wi‑Fi / inbox** — may still be active in parallel with UI

| Symptom pattern | Favor investigating… |
|-----------------|----------------------|
| Guru / LVGL alloc during wipe/reverse | Ledger A pool / masks |
| Long freeze on RED, then collapse | SD commit on UI thread |
| Failure only with Wi‑Fi + send | HTTP + storage ordering |
| Carousel sketch jank while scrolling | PSRAM canvas + invalidate; not `CONFIG_LV_MEM_SIZE` first |

**Measurement gate:** `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC`, saved serial through
RED → receipt, `desk_record_send_check.py`. No `CONFIG_LV_MEM_SIZE` or factory
partition growth without MEM-DELTA evidence or waiver.

## Presentation path (quick)

`on_login_timer` order: buttons → login → inbox → volume → **send** → **record**
→ present. Blocking SD or long encode on RED competes with LVGL flush (internal
DMA draw strip ~10 KiB — separate from 32 KiB object pool).

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
| This skill (brief + investigate + fix) | Strong — synthesis + device loop |
| Independent verifier subagent | Strong for timer path / L3 |

## Anti-patterns in repair sessions

- Jumping to 64 KiB LVGL pool or partition resize before phased measurement
- Blaming “transition” only when RED + SD + Wi‑Fi overlap
- Parallel implementers on one tree
- poc-v1 / x02 essays instead of EXP + COM
- Closing journey on host tests alone
- Mixing guardrail program + Wave 2 without ordering
- “Power cycle required” without retesting mute/BOOT recovery
- Handing off a copy-paste prompt when the user wanted same-session fix
