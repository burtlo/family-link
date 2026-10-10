# PIN keypad component

ID: `COMP-PIN-KEYPAD`  
Type: Component  
Revision: 0.4  
Status: Draft — v1 key sizing/spacing and 3×3 digits 1–9 adopted; no zero key
Owner: Product owner  
Approval: No visual approval claimed  
Target: v2 fixed-hardware 320 × 240 landscape canvas

## Purpose

Numeric keypad for four-digit PIN entry on [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md).

## Layout (owner-decided)

| Rule | Value |
|---|---|
| Grid | **3 × 3** positions for digits **1–9** |
| Digit 0 | Not present; PINs use digits 1–9 only |
| Key geometry | **98 × 42 px** keys, **6 px** horizontal/vertical gaps, **8 px** horizontal canvas inset, adapted from v1 `h03_touch_pin.c` to the lower split-pane slot |
| Clear row | No on-screen clear row specified. BOOT behavior belongs to `EXP-LOGIN` / `DEC-037`: no digits returns to roster; 1–3 digits clear in place; after digit four BOOT is ignored |

Digit order follows conventional phone pad:

```text
1  2  3
4  5  6
7  8  9
```

The v1 geometry reference is [`h03_touch_pin.c`](../../../../poc-v1/firmware/demos/h03_touch_pin.c).
Its 0 and Clear keys are not carried into v2; the owner decided on digits 1–9
only and BOOT-based clearing behavior.

## Typography and touch

- Digit labels: **`fl-type-32`**
- Each key touch target ≥ **`fl-touch-min-height`** (48–56 px band per foundation)

The v1 geometry is retained for the nine keys. Place the three-row grid within
the lower split-pane slot; exact vertical offset and alignment against PIN
progress remain **OPEN**.

## Revision history

- **0.4 — 2026-10-10:** Owner approved the v1 keypad geometry setup; adopted
  h03's 98×42 px key size, 6 px gaps, and 8 px horizontal inset for the 1–9
  grid in the lower split slot.
- **0.3 — 2026-10-10:** Owner confirmed PINs use digits 1–9 only, no zero key;
  conditional BOOT behavior is linked to the experience contract.
- **0.2 — 2026-10-10:** Owner replies confirmed the 1–9 grid but conflicted on
  zero-key and clear behavior; those questions were OPEN at that revision.
- **0.1 — 2026-10-10:** Initial 3×3 keypad draft.
