# Status message

| Field | Value |
|---|---|
| ID | `COMP-STATUS-MESSAGE` |
| Type | Component |
| Revision | 0.3 |
| Status | Draft — in-panel message owner-decided; no card, no icon |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md) `DEC-051`–`DEC-052`, `EXP-005`, `EXP-023`, `EXP-037` |

## Purpose and use

Reusable centered status copy for the lower split-pane slot. Login uses it
for `connecting`, `no Wi-Fi`, and `No people in this hangout`. Each
experience supplies copy and recovery rules.

This component is a leaf inside
[`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md). It is not a
floating card, not a full-screen stage, and it does not dim or cover the
upper panel.

Retry timing stays in `EXP-LOGIN`.

## Anatomy

```text
lower slot_content_bounds
┌────────────────────────────────────────┐
│                                        │
│              connecting                │
│                 · · ·                  │
│                                        │
└────────────────────────────────────────┘
```

| Region | Content | Rule |
|---|---|---|
| Headline | Experience-supplied copy | One centered line. Login uses `connecting`, `no Wi-Fi`, or `No people in this hangout` |
| Progress | Three progressive dots | Only under `connecting`. No icon |

There is no card fill, corner radius, or mailbox mark. The historical 288 px
card and extra Wi-Fi guidance lines are not v2 policy.

## Motion

This component does not slide. The parent panel swaps it into the lower slot
with a 150–200 ms crossfade (`DEC-060`). Cover-down is not used here.

## Content contract and limits

| Variant | Login copy | Shown while |
|---|---|---|
| Waiting | `connecting`, plus three progressive dots | Wi-Fi is available and no valid server response has arrived |
| Link absent | `no Wi-Fi` | Wi-Fi is unavailable |
| Empty hangout | `No people in this hangout` | Valid empty roster; no dots (`DEC-062`) |

- Headline uses the literal strings above.
- Do not add `this box needs the home network` or `ask Lynn to check the network`.
- One message is visible at a time.
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
  maps a connecting main label to `fl-type-32` as guidance. This component
  uses `fl-type-32` for the headline unless a later review changes it.

## Visual states

| State | Presentation |
|---|---|
| Waiting | Centered `connecting`; three dots animate beneath |
| Link absent | Centered `no Wi-Fi`; no dots |
| Empty hangout | Centered `No people in this hangout`; no dots |

Dots animate in place. A dot frame must not clear the whole framebuffer.

## Tokens and assets

Use [`THEME-DARK`](../themes/THEME-DARK.md) text tokens. No icon asset.

## Interaction surface

The component has no buttons. While this message occupies the lower slot,
carousel and keypad are not mounted, so they cannot receive touch. Physical
BOOT is ignored while this status is showing. Mute still applies.

## Accessibility and hardware constraints

- English only, matching `EXP-LOGIN`.
- Headline and dots must remain distinguishable without relying on color alone.
- RGB565 rendering applies. No separate color-blind palette is required.

## Traceability

| Decision | Result |
|---|---|
| `DEC-051`, `EXP-037` | Lower-slot message; keep split layout and upper panel; no card; no dim |
| `DEC-052` | Three dots under `connecting`; no icon on either message |
| `DEC-042`, `DEC-044`, `EXP-005` | Waiting variant stays up through connecting retries |
| `DEC-043`, `EXP-023` | `no Wi-Fi` uses the same component without dots |

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Exact gap between headline and the three dots | Visual approval |

## Readiness

- **Owner review:** In-panel copy, three dots, and no-icon rule are ready for correction.
- **Visual approval:** Pending headline-to-dot spacing.
- **Implementation:** No card geometry to invent. Panel motion is still OPEN.
- **Verification:** No device evidence is claimed.

## Revision history

- **0.3 — 2026-10-21:** Withdraw the overlay card. Center the message in the
  lower panel. Three dots; no icon.
- **0.2 — 2026-10-20:** Overlay card blocked touch and changed copy in place.
  Superseded by 0.3.
- **0.1 — 2026-10-20:** Initial overlay from the login interview. Superseded.
