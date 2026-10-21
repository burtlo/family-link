# PIN status header panel

| Field | Value |
|---|---|
| ID | `PANEL-HEADER-PIN-STATUS` |
| Type | Panel module |
| Revision | 0.2 |
| Status | Draft — identity and PIN slots owner-aligned; checking replaces marks |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Slot | `upper` on [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md), PIN entry (`EXP-014`) |

## Purpose

Shows the selected user's identity and four-digit PIN progress while the user
enters a PIN. The screen mounts this panel in the upper split-pane slot. It
owns the visual composition of identity, PIN slots, and status variants; PIN
verification and state changes remain in `EXP-LOGIN`.

## Chrome and content

| Layer | Rule |
|---|---|
| Background | Paint `fl-color-surface` on the full upper `slot_chrome_bounds` or inherit the shell background |
| Content | Place identity and PIN slots inside upper `slot_content_bounds` (296 × 56 px; x=12…307, y=12…67) |
| Alignment | Identity cluster on the left; PIN slots on the right; vertically center both on the same row |

## Anatomy and typography

```text
upper slot_chrome_bounds (320 × 80)
┌────────────────────────────────────────┐
│  Sign in as [28 px avatar] Name   * * _ _│  slot_content_bounds
└────────────────────────────────────────┘
```

| Region | Presentation |
|---|---|
| Identity cluster | `Sign in as` prefix, 28 px avatar, and display name in `fl-type-24`, weight 600; ellipsize the name at the end |
| PIN slots | Four LTR positions; filled positions show `*` in `fl-color-accent`, empty positions show `_` in `fl-color-text-secondary`; use `fl-type-32` or the nearest linked size if 32 is absent, as recorded by the existing screen spec |
| Row alignment | PIN slots vertically center on the identity row midline, preserving the v1 product placement recorded by the owner |

## Visual states

| State | Presentation | Authority |
|---|---|---|
| Partial PIN | Show entered digits as masked `*` characters and remaining `_` positions | `EXP-014`; `SCR-PIN-ENTRY` |
| Wrong PIN | Replace the identity cluster with literal `wrong pin` for 2 seconds, then restore identity; the experience clears entered digits | `EXP-014`; `SCR-PIN-ENTRY` |
| Checking | Replace the four PIN marks with `checking...`; keep the identity cluster | `DEC-053`, `EXP-014` |
| Cooldown | The screen transitions to [`SCR-PIN-COOLDOWN`](../screens/SCR-PIN-COOLDOWN.md); this panel does not show the cooldown countdown | `EXP-014` |

## Interaction and ownership

This panel is display-only. It creates no touch targets and owns no PIN
verification, retry, clearing, timeout, or navigation policy. The screen and
experience determine the state supplied to it.

## Dependencies and consumers

- [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md)
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
- [`THEME-DARK`](../themes/THEME-DARK.md)
- [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md)

## Readiness

- **Owner review:** Composition reflects the requested panel boundary and existing PIN presentation decisions.
- **Visual approval:** Pending name truncation / slot spacing at the target canvas.
- **Implementation:** Checking placement is decided; linked font fallback remains open on the foundation.

## Revision history

- **0.2 — 2026-10-21:** `checking...` replaces the four PIN marks; identity stays.
- **0.1 — 2026-10-20:** Extract identity and PIN-slot composition from `SCR-PIN-ENTRY` into the upper panel required by the panel contract.
