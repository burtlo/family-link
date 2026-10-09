# Agent guide — Family Link

**Documentation index:** [`README.md`](README.md). Read it before inventing product behavior.

## Repository purpose

Desk **async audio mailbox** for a small hangout on **ESP32-S3-BOX-3** endpoints, with a **home-server** host (`demos/server/v1_product`) and Lynn **web admin**. Island demos (`h01`–`h31`, `p01`–`p13`) are experiments; **product glue** is **`firmware/v1/`** flashed as **`x02`**.

Do not treat v1 architecture as mandatory for a future rewrite. Product intent: [`product/vision.md`](product/vision.md). Approved contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md). As-built features: [`features/README.md`](features/README.md).

## Current boundaries (2026-10-08)

| Track | Rule |
|-------|------|
| **Attached storage qual** | H38 Stage B passed on bound **32 GB** SDHC — see [`ATTACHED-STORAGE.md`](hardware/ATTACHED-STORAGE.md). Do **not** modify `firmware/v1/` or x02 for H38; use isolated demo epoch + mandatory 16 MiB restore. Retired **64 GB** geometry — see ADR [`decisions/retired-64gb-sd-geometry.md`](decisions/retired-64gb-sd-geometry.md). |
| **Product / server** | No-storage roadmap may proceed in parallel — [`plans/README.md`](plans/README.md). Server archive evidence: [`evidence/product-no-storage/03-server-recovery/`](evidence/product-no-storage/03-server-recovery/README.md). |
| **PIN / connectivity bugs** | Scope freeze: [`.cursor/rules/v1-auth-scope-freeze.mdc`](../.cursor/rules/v1-auth-scope-freeze.mdc) — no carousel edits while debugging INT-014 class issues. |

## Build and test

```bash
make v1-server          # product host (see demos/server/v1_product/README.md)
make demo-v1            # smoke
make x02 && make flash DEMO=x02
make v1-timing && make check-v1-parity   # shared/v1/timing.yaml
python3 -m unittest discover -s demos/server/v1_product/tests
```

Combined legacy host (not product): `python -m demos.server.combined.server` — prefer **v1_product** per [`decisions/v1-host-is-v1-product-not-combined.md`](decisions/v1-host-is-v1-product-not-combined.md).

USB serial (Mac): `/dev/cu.usbmodem*`. Flash via **box USB-C**, not dock power-only port. Peel screen protector for mics.

## Where to document changes

| Change type | Update |
|-------------|--------|
| Shipped product behavior | [`features/`](features/) record + [`features/README.md`](features/README.md) status |
| Hardware discovery | [`hardware/`](hardware/) + evidence under [`evidence/`](evidence/) |
| New requirement / backlog item | [`plans/README.md`](plans/README.md) + plan file |
| Locked decision | [`decisions/`](decisions/) ADR |
| Open product choice | [`decisions/open-questions.md`](decisions/open-questions.md) — do not decide silently |

**Plans are not proof of delivery.** Verify against code and tests before marking features verified.

## Cursor rules and skills

| Resource | Use |
|----------|-----|
| [`device-verify-before-done.mdc`](../.cursor/rules/device-verify-before-done.mdc) | Firmware / twin — serial or user confirmation before close |
| [`async-state-no-stale-gates.mdc`](../.cursor/rules/async-state-no-stale-gates.mdc) | v1 async workers |
| [`lvgl-incremental-ui.mdc`](../.cursor/rules/lvgl-incremental-ui.mdc) | x02 UI — 64 KB LVGL heap |
| [`device-test-after-flash`](../.cursor/skills/device-test-after-flash/SKILL.md) | Post-flash checklist |
| [`web-firmware-parity-check`](../.cursor/skills/web-firmware-parity-check/SKILL.md) | timing parity |

Stability synthesis: [`../stability-synthesis.md`](../stability-synthesis.md).

## Experiments vs product

- **Experimental:** demo ids, storage qual epochs, long-message host routes without firmware adoption, personality demos.
- **Product:** x02 + v1_product server paths described in [`features/`](features/).

When stuck: [`decisions/open-questions.md`](decisions/open-questions.md). Do not add wake word, e-ink, or canned phrases to “make progress.”
