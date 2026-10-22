# Missing device UI components

Status: planning inventory, 2026-10-11. These are proposed artifact IDs, not approved v2 specifications. The legacy v1 implementation is evidence of what was visible, not authority for v2 behavior or geometry. Use the active experience specifications for decisions and the [UI index](../README.md) for existing artifacts.

## Already covered

[`COMP-USER-CARD`](COMP-USER-CARD.md) covers roster and recipient-card states; [`COMP-MESSAGE-CARD`](COMP-MESSAGE-CARD.md) now drafts inbox-card geometry and metadata; [`COMP-PIN-KEYPAD`](COMP-PIN-KEYPAD.md) covers the digit grid; [`COMP-RECORD-CANVAS`](COMP-RECORD-CANVAS.md) and [`COMP-COLOR-SWATCH`](COMP-COLOR-SWATCH.md) cover the record sketch. Extend those only where the new use genuinely shares their visual contract.

## Missing artifacts

| Proposed document | Specific gap and v1 evidence | Follow-up authoring brief |
|---|---|---|
| `COMP-STATUS-MESSAGE.md` | Authored as [`COMP-STATUS-MESSAGE`](COMP-STATUS-MESSAGE.md). In-panel copy, three dots, and no-icon rule are decided. The overlay card is withdrawn. | Do not treat the historical 288 px card or extra Wi-Fi guidance lines as v2 policy. |
| `COMP-MESSAGE-CARD.md` | Drafted as [`COMP-MESSAGE-CARD`](COMP-MESSAGE-CARD.md). Owner-aligned no-sketch portrait/name/`m:ss` layout and sketch replacement; no unread dot; eight card fills selected. Avatar art and final visual fit remain open. | Use the interim initials fallback from [`FOUNDATION-PROFILE-AVATARS`](../foundations/FOUNDATION-PROFILE-AVATARS.md), then visually verify component fit. Use [`ASSESS-MODULE-v1-carousel`](../assessments/ASSESS-MODULE-v1-carousel.md) for resource and redraw evidence. |
| `COMP-PLAYBACK-TRANSPORT.md` | V1 puts play/pause in the header, updates it during playback, and creates a scrub slider but keeps it hidden. No reusable transport specification exists. | Decide visible controls and progress affordance with the owner; specify disabled/buffering/playing/paused/ended/error states and touch geometry. Keep playback policy in a future signed-in experience contract. Do not treat the hidden slider as a v2 requirement. |
| `COMP-SETTINGS-CARD.md` | V1 reuses its carousel for six setting tiles: Logout, Volume, Auto-play new, User icons, User colors, Card colors. | Specify tile anatomy, value summary, focus/pressed/disabled state, truncation, and distinction from a message card. First confirm which settings survive in v2; settings behavior and persistence need an experience contract. |
| `COMP-RECIPIENT-ALL-CARD.md` | The v1 recipient picker adds an Everyone tile after person cards, using an asterisk symbol. | Define the Everyone visual and selection state, including how it differs from `COMP-USER-CARD` and how mixed selection is shown. Confirm recipient semantics before visual approval. |
| `COMP-RECORD-CANVAS.md` | Authored as [`COMP-RECORD-CANVAS`](COMP-RECORD-CANVAS.md). 240×240 black canvas, 8 px corners, 2× logical mapping. | Unavailable/error chrome remains OPEN on the panel. |
| `COMP-COLOR-SWATCH.md` | Authored as [`COMP-COLOR-SWATCH`](COMP-COLOR-SWATCH.md). 44×44 circle, gray ring, gold selected outline, circular hit. | Placement of the six chips stays on [`PANEL-SIDE-COLOR-PALETTE`](../panels/PANEL-SIDE-COLOR-PALETTE.md). |
| `COMP-STATUS-INDICATOR.md` | V1 shares ribbon/toast/offline indicators across signed-in stages, uses three progressive dots for connecting, and shows send success/failure symbols. These are currently implicit in screen code. | Define small indicator anatomy and animation where it is reused by `COMP-STATUS-MESSAGE` or chrome. Keep it separate from the larger text-and-guidance message component; decide which marks are genuinely shared. |

## Handoff questions

1. Signed-in browsing/playback behavior is specified in [`EXP-MESSAGE-CAROUSEL`](../../experiences/message-carousel-and-playback.md). The message-card draft and eight-color background catalog are authored; avatar art and target-hardware fit remain follow-up. Visible transport is limited to the labeled header Play/Pause control; no scrubber is specified.
2. Author settings and recipient-selection behavior before treating the v1 tiles or Everyone card as v2 commitments.
3. Record native-pixel geometry, state table, dependencies, `UI-OPEN-###` items, and separate owner/visual/implementation readiness in each resulting `COMP-*` file. Check 320×240 legibility and the [LVGL budget](../foundations/FOUNDATION-LVGL-BUDGET.md).
