# Status message

| Field | Value |
|---|---|
| ID | `COMP-STATUS-MESSAGE` |
| Type | Component |
| Revision | 0.2 |
| Status | Draft — overlay role, copy, blocked touch, and motion owner-decided; geometry OPEN |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md) `DEC-041`–`DEC-047`, `EXP-005`, `EXP-023`, `EXP-037`–`EXP-038` |

## Purpose and use

Reusable information overlay for short status and error messages. Login uses it
for `connecting` and `no Wi-Fi`. Later experiences may reuse it for other
information and error messages; each experience supplies its own copy and
recovery rules.

This component is an overlay leaf. It is not a split-pane slot panel and it
does not replace the login screen. The current login surface stays underneath.

It does not decide retry timing, readiness, or which condition is active.
Those rules stay in `EXP-LOGIN`.

## Anatomy

The panel rests at the center of the 320 × 240 canvas. Width, height, corner
radius, padding, icon slot, and dot size are OPEN.

```text
(0,0)                         (320,0)
┌────────────────────────────────────────┐
│                                        │
│         ┌──────────────────┐           │
│         │  connecting      │           │
│         │      · · ·       │           │
│         └──────────────────┘           │
│                                        │
└────────────────────────────────────────┘
(0,240)                       (320,240)
```

| Region | Content | Rule |
|---|---|---|
| Card | One short message | Centered on the canvas. Fill, radius, and size are OPEN |
| Headline | Experience-supplied copy | One line. Login uses `connecting` or `no Wi-Fi` |
| Progress | Progressive dots | Shown for the waiting variant only. Dot count is OPEN |
| Explanation | Optional second line | Login does not use one. Other callers may supply one later |

Historical message cards used a fixed width and one or two extra guidance
lines. Those measurements and the extra Wi-Fi lines are not v2 policy.

## Motion

This motion is not [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md).
Cover-down brings a full screen to `y = 0`. This card stops at the center.

| Event | Motion |
|---|---|
| Condition becomes active | Card enters from the bottom edge and comes to rest at the canvas center |
| Condition clears | Card slides down from center and leaves the bottom edge |
| Condition changes to the other login message | Stay centered and change the headline in place |

Duration, easing, and whether the login surface dims are OPEN. While the card
is visible, including during the slide, all touch on the surface underneath
is ignored.

## Content contract and limits

| Variant | Login copy | Shown while |
|---|---|---|
| Waiting | `connecting`, plus progressive dots | Wi-Fi is available and no valid server response has arrived |
| Link absent | `no Wi-Fi` | Wi-Fi is unavailable |

- Headline uses the literal strings above. Do not add the historical lines
  `this box needs the home network` or `ask Lynn to check the network`.
- One message is visible at a time.
- Copy that does not fit the still-open card is OPEN. Login’s two headlines
  are short enough for one line at `fl-type-32` or `fl-type-24`.
- [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md)
  maps a connecting main label to `fl-type-32` as guidance. This component
  does not yet lock that size.

## Visual states

| State | Presentation |
|---|---|
| Hidden | No card. The login surface is unobstructed |
| Entering | Card moves from below the canvas toward center |
| Waiting | Centered card, headline `connecting`, progressive dots animate |
| Link absent | Centered card, headline `no Wi-Fi`, no dots |
| Copy change | Same centered card; headline and dots swap without a second slide |
| Leaving | Card moves from center down off the canvas |

Dots animate in place. A dot frame must not clear the whole framebuffer.

## Tokens and assets

Use [`THEME-DARK`](../themes/THEME-DARK.md) tokens once a card fill and text
color are chosen. No icon is decided. A mailbox mark and a crossed Wi-Fi mark
are historical evidence only.

## Interaction surface

The card itself has no buttons in the login uses. It covers roster, empty
hangout, PIN entry, PIN verification, and cooldown. While it is visible, all
touch on the surface underneath is ignored, including PIN digits. Physical
mute still applies. Physical BOOT is ignored while the card is up.

## Accessibility and hardware constraints

- English only, matching `EXP-LOGIN`.
- Headline and dots must remain distinguishable without relying on color alone.
- The card must stay inside the 320 × 240 canvas at rest.
- RGB565 rendering applies. No separate color-blind palette is required.

## Traceability

| Decision | Result |
|---|---|
| `DEC-041`, `EXP-037` | Centered overlay; slide up to appear; slide down only when neither condition remains |
| `DEC-042`, `DEC-044`, `EXP-005` | Waiting variant stays up through 3-second attempts and 1/2/4/30-second waits |
| `DEC-043`, `DEC-047`, `EXP-023` | `no Wi-Fi` and `connecting` share one card; copy changes in place |
| `DEC-045` | Covers PIN and blocks all touch |
| `DEC-046`, `DEC-048`, `EXP-038` | Empty-hangout layout sits underneath before the first roster, with heading `Loading hangout ...` |
| `DEC-050` | Ignore BOOT while visible |

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Card width, height, corner radius, padding, and resting bounds | Visual approval |
| `UI-OPEN-002` | Headline type size, card fill, text color, and whether the surface behind the card dims | Visual approval |
| `UI-OPEN-003` | Progressive-dot count, size, gap, and frame timing | Waiting-state implementation |
| `UI-OPEN-004` | Slide duration and easing | Motion implementation |
| `UI-OPEN-005` | Whether either login message includes an icon | Visual approval |

## Readiness

- **Owner review:** Role, login copy, and slide direction are ready for correction.
- **Visual approval:** Not ready. Size, color, dots, and icons are OPEN.
- **Implementation:** Not ready. An implementer would have to invent the card geometry.
- **Verification:** No device evidence is claimed.

## Revision history

- **0.2 — 2026-10-20:** Block all touch; cover PIN; change copy in place;
  empty-hangout may sit underneath.
- **0.1 — 2026-10-20:** Initial overlay from the login interview. Copy and
  slide direction recorded. Geometry left OPEN. Full-screen connecting and
  Wi-Fi screens are not the login presentation.
