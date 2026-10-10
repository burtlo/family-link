# Device motion foundation

ID: `FOUNDATION-MOTION`  
Type: Foundation  
Revision: 0.1  
Status: Draft — owner direction recorded; timing unverified  
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware device UI (LVGL)

## Purpose

Records cross-screen and in-shell motion **intent** before firmware proves what
the display stack can do reliably. v1 implementation did not fully explore
transitions; treat durations and easing as proposals until measured.

## Owner direction

| Use case | Intended treatment | Status |
|---|---|---|
| Stage change (for example roster → PIN) | **Cross-fade** between screen roots | **PROPOSED** — verify on device |
| Carousel snap | Scale/opacity focus change (see `COMP-USER-CARD`) | **Owner-aligned** with v1 as-built reference |
| Panel module swap within one shell | Prefer cross-fade or instant cut; no slide unless specified | **OPEN** |

## Engineering discovery (required before normative timing)

1. LVGL opacity / transform performance at 320 × 240 RGB565.
2. Whether cross-fade of full framebuffer is acceptable versus dual-buffer blend.
3. Interaction with **Level 2** UI updates (see UI update taxonomy).
4. Whether touch must be disabled for the fade duration (carousel already locks
   during snap in v1 reference).

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Cross-fade duration and easing for stage transitions | `SCR-*` motion columns |
| `UI-OPEN-002` | Fallback when fade is too slow or unsupported | Implementation |

## Revision history

- **0.1 — 2026-10-10:** Cross-fade proposed for stage transitions; discovery
  outstanding.
