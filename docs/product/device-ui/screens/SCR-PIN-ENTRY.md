# PIN entry screen

| Field | Value |
|---|---|
| ID | `SCR-PIN-ENTRY` |
| Type | Screen composition |
| Revision | 0.4 |
| Status | Draft — shell, identity, masked PIN progress, error placement, keypad geometry, and BOOT behavior decided; remaining states OPEN |
| Owner | Product owner |
| Approval | No visual approval claimed |
| Target | v2 fixed-hardware 320 × 240 landscape canvas |
| Experience | [`EXP-LOGIN`](../../experiences/endpoint-login.md), PIN entry and verification stages |

## Purpose and scope

This screen lets a person enter the selected user's four-digit PIN and see the
visual states required by the login experience. The experience specification
owns PIN behavior, server operations, cancellation, timeout, retry, and result
handling. This screen specification owns visual composition and presentation.

The owner decided that keypad layout, success feedback, and cooldown presentation
belong in a PIN screen specification. Cooldown wording is neutral:
“Too many attempts. Try again in <seconds>”. The layout and countdown treatment
are not yet decided. Wake, roster-ready, and eight-user update timing budgets
will be measured and proposed by engineering; this screen does not set those
budgets.

## Sources and authority

| Source | Authority | Applicable material |
|---|---|---|
| `EXP-LOGIN`, `DEC-009`, `DEC-033–035`, `DEC-040` | Owner-decided experience contract | Four-digit PIN flow, feedback, five wrong-PIN failures followed by a 60-second cooldown, v1 counter behavior, neutral cooldown copy, split-pane, name/avatar, right-justified masked progress and error placement |
| `EXP-017`, `EXP-022`, `EXP-030–031`, `EXP-033`, `EXP-037` | Owner-decided experience contract | Async verification, transport retry limit/timing, conditional BOOT behavior, mute cancellation, partial-entry timeout |
| [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) | Draft visual foundation | Native canvas, typography, spacing, and touch-target tokens |
| [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) | Draft visual layout | Owner-decided shared ⅓/⅔ shell for PIN entry |
| Client application coding standards | Normative engineering constraints | Input/render callbacks must not wait on authentication I/O |

No v1 visual detail is adopted as a v2 requirement unless accepted in the
experience or this screen specification.

## Composition

PIN entry uses the same split shell as the sign-in roster. The upper region shows
selected-user name, avatar, and right-justified masked PIN progress; the lower
region contains a 3×3 keypad for digits 1–9. PINs cannot contain zero. A wrong
PIN replaces the selected-user header with the error message briefly, then
clears the entry and restores the identity header.

```text
SCR-PIN-ENTRY
└─ LAYOUT-SPLIT-PANE
   ├─ upper → selected-user name/avatar, status, and right-justified PIN stars
   └─ lower → COMP-PIN-KEYPAD (3×3 grid for digits 1–9; v1 key geometry)
```

See [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md). The upper identity
panel is not yet authored.

## Canvas and wireframe

Target canvas: 320 × 240 native pixels, landscape, origin at upper-left. The
wireframe shows the decided hierarchy, not a geometry spec.

```text
(0,0)                                   (320,0)
┌────────────────────────────────────────┐
│  [avatar] user name       ** PIN status│  upper 80 px
│                                        │
├────────────────────────────────────────┤
│                                        │
│          1  2  3                       │
│          4  5  6                       │  lower 160 px
│          7  8  9                       │
│                                        │
└────────────────────────────────────────┘
(0,240)                               (320,240)
```

Shell geometry: 80 px upper / 160 px lower per
[`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md). The selected user's
name and avatar appear in the upper region, with PIN stars right-justified.
Wrong-PIN feedback replaces the identity heading briefly, then entry clears and
the identity returns. The keypad uses v1 h03 geometry: 98×42 px keys, 6 px
gaps, and 8 px horizontal canvas inset, adapted to the lower slot. The exact
vertical placement remains OPEN.

## Visual states inherited from the experience

The following state meanings and transitions come from `EXP-LOGIN`; this screen
defines their visual presentation only:

| State | Required visible content | Presentation status |
|---|---|---|
| PIN partially entered | One right-justified masked `*` per entered digit | Mask and alignment decided; exact vertical placement OPEN |
| Checking | Four progress marks and `checking...`; input is pending | Placement and visual treatment OPEN |
| Wrong PIN | `wrong pin` replaces the selected-user heading briefly; then entry clears and identity returns | Copy and placement decided; duration remains OPEN |
| Cooldown | Neutral message “Too many attempts. Try again in <seconds>” | Copy intent decided; countdown format, placement, and emphasis OPEN |
| Success | Transition to the signed-in message carousel | No success screen on this composition; any transition feedback OPEN |
| BOOT | With no digits, return to roster; with 1–3 digits, clear them and remain; after digit four, ignore BOOT during verification | Behavior is fixed by `EXP-030` / `DEC-037`; visual implementation OPEN |
| Mute | Cancel login and turn display off | Experience behavior fixed; no additional visual behavior defined here |

The experience's 60-second partial-PIN inactivity timeout, four total transport
attempts, one-second attempt timeout, 1/2/4-second transport delays, and 60-second
wrong-PIN cooldown are behavior requirements. These durations must not be
reinterpreted as animation durations.

## Content and interaction boundaries

- The upper area shows the selected user's **name and avatar**, plus right-
  justified PIN status/progress. Wrong-PIN feedback temporarily replaces the
  identity header.
- Keypad: 3×3 digits **1–9 only**, no zero key. Key size and spacing follow
  v1 h03; vertical placement within the lower slot remains OPEN. No on-screen
  clear control is specified.
- BOOT follows `EXP-LOGIN` / `DEC-037`: no digits returns to roster; 1–3 digits
  clears in place; after the fourth digit BOOT is ignored during verification.
- The experience automatically begins asynchronous verification after the
  fourth digit; the screen must not imply that a second submit action is
  required.
- The keypad must communicate when input is ignored during verification or
  cooldown; exact disabled/focus treatment is OPEN.
- The neutral cooldown copy is fixed by `DEC-034`; changing it requires revising
  the experience decision.
- No screen reader, touch-less use, non-English localization, or extra
  color-blind support requirement is specified in the current experience.

## Continuity and update mapping

- PIN dots and status text are property updates within the current screen.
- Authentication completion replaces this screen with the signed-in message
  carousel.
- BOOT returns to selection only before any digit is entered; with 1–3 digits
  it clears in place, and after digit four it is ignored during verification.
  Mute cancels and turns off the display. These behaviors are owned by
  `EXP-LOGIN` / `DEC-037`.
- Input handlers and rendering must remain independent of network wait time,
  following the client application coding standards.

Update levels follow the
[UI update taxonomy](../../../v1-assessments/ui-update-taxonomy.md).

## Traceability

| Decision/requirement | Screen result | Composition |
|---|---|---|
| `DEC-009`, `DEC-033`, `EXP-014` | Four-digit flow, partial/wrong/cooldown states | PIN status panel and keypad panel (planned) |
| `DEC-034` | Separate screen owns visual details; neutral cooldown copy | `SCR-PIN-ENTRY` |
| `EXP-017`, `EXP-030`, `EXP-033`, `DEC-037` | BOOT follows entry-progress rules; mute cancels and turns display off | Conditional screen replacement / clear-in-place / display off |
| `EXP-022` | Timeout retries and terminal connecting outcome | Checking state; behavior remains in experience |
| `EXP-031` | Partial-entry timeout return | Partial PIN state ends; behavior remains in experience |

## OPEN items

| ID | Question | Impact | Blocks |
|---|---|---|---|
| `UI-OPEN-001` | *(Resolved 2026-10-10)* Use `LAYOUT-SPLIT-PANE`; upper area shows selected-user identity and PIN status | — | — |
| `UI-OPEN-002` | *(Resolved 2026-10-10)* Show selected-user name and avatar | — | — |
| `UI-OPEN-003` | *(Resolved 2026-10-10)* PIN digits are 1–9 only; no zero key; adopt v1 h03 key sizing and spacing | — | — |
| `UI-OPEN-004` | Specify checking, disabled, and success-transition visual treatments; set transient wrong-PIN message duration and PIN/keypad vertical placement | State recognition and feedback | Full screen implementation |
| `UI-OPEN-005` | Place and format the neutral cooldown message/countdown | Lockout comprehension | Cooldown state implementation |
| `UI-OPEN-006` | Decide colors, type sizes, contrast, and any motion using the shared foundation | Legibility and visual consistency | Visual approval |

## Readiness

- **Owner review:** Shell, name/avatar identity, masked right-justified progress,
  wrong-PIN replacement treatment, digits 1–9, v1 keypad geometry, and BOOT
  behavior are decided; some state treatments and placement remain open.
- **Visual approval:** Not ready; `UI-OPEN-004–006` remain unresolved.
- **Implementation:** Not ready; screen geometry and components are not
  specified.
- **Verification:** No implementation evidence exists. Timing budgets are an
  engineering measurement/proposal outside this visual specification.

## Revision history

- **0.4 — 2026-10-10:** Recorded right-justified masked PIN progress, wrong-PIN
  replacement of the identity header, and v1 h03 keypad geometry.
- **0.3 — 2026-10-10:** Recorded selected-user name/avatar, digits 1–9 only,
  and conditional BOOT behavior from the experience contract.
- **0.2 — 2026-10-10:** Split-pane and selected-user identity/status hierarchy
  confirmed. Conflicting answers on whether to include a zero key were OPEN.
- **0.1 — 2026-10-10:** Initial draft created from `EXP-LOGIN` and the owner's
  delegation of PIN visual presentation. No visual approval claimed.
