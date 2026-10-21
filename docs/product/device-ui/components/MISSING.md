# Missing device UI components

Status: planning inventory, 2026-10-11. These are proposed artifact IDs, not approved v2 specifications. The legacy v1 implementation is evidence of what was visible, not authority for v2 behavior or geometry. Use the active experience specifications for decisions and the [UI index](../README.md) for existing artifacts.

## Already covered

[`COMP-USER-CARD`](COMP-USER-CARD.md) covers roster and recipient-card states; [`COMP-PIN-KEYPAD`](COMP-PIN-KEYPAD.md) covers the digit grid. Extend those only where the new use genuinely shares their visual contract.

## Missing artifacts

| Proposed document | Specific gap and v1 evidence | Follow-up authoring brief |
|---|---|---|
| `COMP-STATUS-MESSAGE.md` | Authored as [`COMP-STATUS-MESSAGE`](COMP-STATUS-MESSAGE.md). In-panel copy, three dots, and no-icon rule are decided. The overlay card is withdrawn. | Do not treat the historical 288 px card or extra Wi-Fi guidance lines as v2 policy. |
| `COMP-MESSAGE-CARD.md` | The signed-in inbox has a 132 px wide card with portrait, unread dot, duration, and optional sketch imagery. A user card does not specify message metadata or playback imagery. | Define card anatomy, 120×120 sketch viewport and outer chrome from [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md), focused/peek/unread/playing/loading/failure states, text overflow, and update boundaries. Use [`ASSESS-MODULE-v1-carousel`](../assessments/ASSESS-MODULE-v1-carousel.md) for resource and redraw evidence. |
| `COMP-PLAYBACK-TRANSPORT.md` | V1 puts play/pause in the header, updates it during playback, and creates a scrub slider but keeps it hidden. No reusable transport specification exists. | Decide visible controls and progress affordance with the owner; specify disabled/buffering/playing/paused/ended/error states and touch geometry. Keep playback policy in a future signed-in experience contract. Do not treat the hidden slider as a v2 requirement. |
| `COMP-SETTINGS-CARD.md` | V1 reuses its carousel for six setting tiles: Logout, Volume, Auto-play new, User icons, User colors, Card colors. | Specify tile anatomy, value summary, focus/pressed/disabled state, truncation, and distinction from a message card. First confirm which settings survive in v2; settings behavior and persistence need an experience contract. |
| `COMP-RECIPIENT-ALL-CARD.md` | The v1 recipient picker adds an Everyone tile after person cards, using an asterisk symbol. | Define the Everyone visual and selection state, including how it differs from `COMP-USER-CARD` and how mixed selection is shown. Confirm recipient semantics before visual approval. |
| `COMP-RECORD-CANVAS.md` | V1 records on a 320×240 image; active v2 intent changes this to a 240×240 display canvas mapped 2× to a 120×120 logical sketch. | Specify background, 8 px corner treatment, clipping, stroke preview, touch boundary, empty/active/unavailable states, and visual correspondence to the message-card sketch. Use `SK-001`–`SK-004` and `EXP-SKP-001`–`003`; do not port v1 coordinates. |
| `COMP-COLOR-SWATCH.md` | V1 has one ink color; the active v2 record experience calls for six 44×44 rail swatches with selected outline. | Specify swatch fill, border/selection/focus states, hit area, RGB565 rendering expectations, and background-color swatch treatment from `SK-006`–`SK-007`. The containing rail belongs in a panel. |
| `COMP-STATUS-INDICATOR.md` | V1 shares ribbon/toast/offline indicators across signed-in stages, uses three progressive dots for connecting, and shows send success/failure symbols. These are currently implicit in screen code. | Define small indicator anatomy and animation where it is reused by `COMP-STATUS-MESSAGE` or chrome. Keep it separate from the larger text-and-guidance message component; decide which marks are genuinely shared. |

## Handoff questions

1. Author a signed-in browsing/playback experience before locking message-card and transport behavior: empty inbox, autoplay, offline playback, scrub, and failure policy remain open in [carousel assessment](../../../v1-assessments/carousel-playback.md).
2. Author settings and recipient-selection behavior before treating the v1 tiles or Everyone card as v2 commitments.
3. Record native-pixel geometry, state table, dependencies, `UI-OPEN-###` items, and separate owner/visual/implementation readiness in each resulting `COMP-*` file. Check 320×240 legibility and the [LVGL budget](../foundations/FOUNDATION-LVGL-BUDGET.md).
