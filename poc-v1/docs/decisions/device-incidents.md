# Device incidents index

Structured index for on-device failures, stability analysis, and related evidence. **Do not delete** the root compilations — link them here.

## Primary compilations

| Document | Scope |
|----------|--------|
| [`interviews.md`](../../interviews.md) | INT-001–INT-015 from agent transcripts (Sep 2026): WiFi, PIN/connecting, carousel, LVGL, build blockers |
| [`stability-synthesis.md`](../../stability-synthesis.md) | Cross-incident patterns, anti-patterns, prioritized fixes, Cursor rules linkage |

Prior narrative: [`first-interview.md`](../../first-interview.md) (feeds INT-014).

## Incident quick reference

| ID | Symptom (short) | Outcome (as of compile date) |
|----|-----------------|------------------------------|
| INT-001 | WiFi failed on boot | Partial — needs user creds |
| INT-002 | Play icon; swipe next message | Fixed |
| INT-003 | Play needs multiple taps after message change | Fixed (user unconfirmed) |
| INT-004 | Roster stale when server dies | Fixed; PIN hang recurred → INT-014 |
| INT-005 | Connecting dots misaligned / crawl | Fixed |
| INT-006–INT-010 | p13 white screen, flash, freeze, fonts | Mostly fixed |
| INT-011–INT-013 | Carousel touch/snap | Partial / unconfirmed |
| INT-014 | PIN stuck on `checking...` | **Unresolved** — scope freeze: [`.cursor/rules/v1-auth-scope-freeze.mdc`](../../.cursor/rules/v1-auth-scope-freeze.mdc) |
| INT-015 | x02 build failed (blocked flash) | Fixed |

Acceptance for INT-014: [`BOX-UI.md`](../BOX-UI.md) § PIN verify; implementation `firmware/v1/v1_auth.c`. Plan: [`plans/v1-isolation-plan.md`](../plans/v1-isolation-plan.md).

## Verification norms

| Resource | Use |
|----------|-----|
| [`.cursor/rules/device-verify-before-done.mdc`](../../.cursor/rules/device-verify-before-done.mdc) | Serial log or user confirmation before closing firmware tasks |
| [`.cursor/skills/device-test-after-flash/SKILL.md`](../../.cursor/skills/device-test-after-flash/SKILL.md) | Post-flash checklist |
| [`.cursor/rules/async-state-no-stale-gates.mdc`](../../.cursor/rules/async-state-no-stale-gates.mdc) | Async worker / UI reconciliation |
| [`.cursor/rules/lvgl-incremental-ui.mdc`](../../.cursor/rules/lvgl-incremental-ui.mdc) | LVGL heap and incremental paint |

## Related evidence paths

| Area | Path |
|------|------|
| Product baseline / INT-014 gate | [`plans/product-00-baseline-gate.md`](../plans/product-00-baseline-gate.md), [`plans/v1-isolation-remaining.md`](../plans/v1-isolation-remaining.md) |
| Server/device contract gaps | [`plans/product-without-removable-storage-assessment.md`](../plans/product-without-removable-storage-assessment.md) |
| Storage qual (not device UI) | [`evidence/onchip-storage-qualification/`](../evidence/onchip-storage-qualification/), [`evidence/attached-storage-qualification/`](../evidence/attached-storage-qualification/) |
| x02 timing parity | `make check-v1-parity`; [`shared/v1/timing.yaml`](../../shared/v1/timing.yaml) |

## Decisions cross-links

- PIN and recording gates: [ADR-001](async-audio-v1-scope.md), [`plans/v1-product-spec.md`](../plans/v1-product-spec.md)
- Open UX/network items: [`open-questions.md`](open-questions.md)
