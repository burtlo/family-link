# Missing device UI layouts

Status: planning inventory, 2026-10-11. Proposed IDs are handoff targets, not approved layouts. The [existing split-pane shell](LAYOUT-SPLIT-PANE.md) covers the login composition; the legacy v1 stage renderer also used full-screen hero, message, and recording compositions.

| Proposed document | Specific gap and v1 evidence | Follow-up authoring brief |
|---|---|---|
| `LAYOUT-FULL-CANVAS.md` | The current index calls for an explicit full-canvas layout, but none exists. V1 connecting and Wi-Fi error paint directly on 320×240, outside split panes. | Define shell background, safe content region, clipping, and panel mount rules for centered message/hero stages. Keep copy and stage behavior in screens. Confirm whether sleep/display-off needs any visible composition (usually none). |
| `LAYOUT-CAROUSEL-STAGE.md` | V1 signed-in inbox, settings, and recipient picker share top/bottom ribbons, a sender/title band, and a horizontal card band. The current split pane has different inset ownership. | Specify a reusable 320×240 slot map: status chrome, header, card viewport, controls, bleed, and content bounds. Decide whether to adapt `LAYOUT-SPLIT-PANE` or create this distinct ID after comparing native-pixel geometry. Do not copy v1 ribbon heights automatically. |
| `LAYOUT-RECORD-CANVAS-RAIL.md` | V1 uses a full-screen drawing surface, but v2 `SK-004`–`SK-005` specify a 240×240 left canvas and 80×240 right rail. No layout assigns these regions. | Lock x=0…239 canvas and x=240…319 rail, no overlap, touch boundary, background ownership, and layering. Inherit [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md). The v2 experience excludes a timer and record disk from this steady-state composition. |
| `LAYOUT-CENTERED-RECEIPT.md` | V1 send phases use a centered headline, optional success mark, portrait row or Everyone symbol, and recipient summary. No shell defines these placements. | Decide whether this is a distinct layout or a `LAYOUT-FULL-CANVAS` arrangement. Define reserved regions for long names, multiple recipients, and failure copy; leave phase policy to a send experience contract. |

## Handoff checks

- Draw native-pixel wireframes with `slot_chrome_bounds` and `slot_content_bounds`; state which panel owns margins and background per [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md).
- Compare each shell with the 320×240 target, [`THEME-DARK`](../themes/THEME-DARK.md), and [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md).
- Treat ribbon geometry and v1 full-screen record geometry as historical observations. Active v2 sketch geometry wins where it conflicts.
