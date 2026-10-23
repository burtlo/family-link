# Volume slider body panel

| Field | Value |
|---|---|
| ID | `PANEL-BODY-VOLUME-SLIDER` |
| Type | Panel module |
| Revision | 0.1 |
| Status | Draft — stop set decided; track pixels OPEN |
| Owner | Product owner |
| Slot | `lower` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-SETTINGS`](../../experiences/settings.md) `DEC-010`, `EXP-011` |

## Purpose

Body control for desk speaker volume. Shows a horizontal slider only. **Do not**
paint the codec register number or any numeric readout (`EXP-SETTINGS` excludes
v1 numeric label).

Audible stops are silent, then 78 through 100 inclusive in steps of 2 (13
stops). Persist and speaker routing are product behavior in P9.

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Exact track thickness, thumb size, and horizontal inset within the lower slot | LAB-14 stand-in geometry |

## Dependencies

- [`SCR-VOLUME`](../screens/SCR-VOLUME.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) — `fl-slider-track-min`
