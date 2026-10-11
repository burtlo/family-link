# LVGL assessment — demo p12 (Tamagotchi CPU framebuffer)

**Framework:** [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)  
**Pattern ledger:** **ADAPT**

## Header

| Field | Value |
|---|---|
| **Assessment ID** | `ASSESS-DEMO-p12` |
| **Scope** | Personality demo `p12` (creature in CPU FB, mic reactive) |
| **Revision** | 0.1 |
| **Status** | Draft (static) |
| **Date** | 2026-10-11 |
| **Related specs** | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |

## 1. User-visible scope

Creature bounces when quiet; “listens” (gold) when speech over threshold; mute latch gates mic.

## 2. Pattern (documented in header)

**Avoid** animating many LVGL widgets on SPI — **tear**. Instead: paint into **320×240** FB, single `lv_image`, invalidate.

## 3. Measurement

| Column | Target |
|---|---|
| Ledger B | **153 600** B FB + codec buffers |
| Ledger A | ~1 image + labels |
| Invalidate | Per animation frame — similar stress to h28 at lower duty cycle |

## 8. Recommendations

| Priority | Recommendation |
|---|---|
| P0 | **ADOPT** “one blit surface” lesson for fast motion on BOX-3 |
| P1 | Prefer **partial dirty rects** or smaller FB if mascot is not full-screen |
| P2 | Not product personality v2 unless explicitly scoped |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial |
