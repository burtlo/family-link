# Missing device UI screens

Status: planning inventory, 2026-10-11. The four existing login screens are indexed in the [device UI README](../README.md). This file lists the remaining visible x02 stages and screen variants. Proposed IDs do not claim v2 product approval.

| Proposed document | Specific gap and v1 evidence | Composition and handoff |
|---|---|---|
| `SCR-CONNECTING.md` | Not a separate screen. `EXP-LOGIN` `DEC-041` shows `connecting` as [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) over the current login surface. | Do not author a full-canvas replacement. Geometry stays on the component. |
| `SCR-WIFI-UNAVAILABLE.md` | Not a separate screen. `DEC-043` shows `no Wi-Fi` in the same overlay. The historical extra guidance lines are not adopted. | Do not author a full-canvas replacement. Keep this condition distinct from connecting. |
| `SCR-MESSAGE-INBOX.md` | Successful login enters a signed-in message carousel (`EXP-012`), but no screen composes its header, cards, and transport. | Compose carousel-stage layout, status chrome, sender header, inbox body, message card and transport. Specify empty, one/many, unread, refreshing, playing and unavailable visual states. First author signed-in browsing/playback behavior. |
| `SCR-SETTINGS.md` | V1 exposes a settings carousel and detail overlays; no v2 screen exists. | Compose settings body and detail overlay if the owner retains this stage. Resolve setting set, values and persistence in an experience specification before locking screen variants. |
| `SCR-RECIPIENT-PICKER.md` | V1 moves from inbox to a horizontal recipient carousel with Everyone and possible multi-select. | Compose picker header/body and the selected-recipient visual. Specify empty roster, one/many, Everyone, focus versus selection, and back/record transition; author recipient-selection behavior first. |
| `SCR-RECORD-SKETCH.md` | V1 has a 320×240 draw screen with disk/timer. Active v2 `EXP-SKETCH-RECORD-PLAYBACK` specifies 240×240 paint canvas plus 80×240 color rail; h19 showcase has no timer or record indicator. | Compose `LAYOUT-RECORD-CANVAS-RAIL`, canvas component, and color rail panel. Document blank, drawing, selected-color, unavailable, and exit visuals. Preserve `OPEN-SKP-05` for later owner decision; do not reintroduce v1 chrome by default. |
| `SCR-SEND-PROGRESS.md` | V1 shows Finishing and Sending... after recording. | Compose receipt layout + send feedback panel; specify visual continuity from capture and any recipient summary during wait. Send lifecycle and whether carousel remains visible are still product decisions. |
| `SCR-SEND-RESULT.md` | V1 shows Sent or Couldn't send, then returns to inbox. | Specify success, failure, and any partial-result visual variants, names/Everyone display, copy overflow, and user action. A future send experience must decide retry, partial delivery, and receipt duration. This may be one screen with `SCR-SEND-PROGRESS` if a shared composition is confirmed. |

## Coverage boundary and follow-up order

1. [`EXP-LOGIN`](../../experiences/endpoint-login.md) owns connecting and Wi-Fi behavior. Those messages are [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) overlays, not screens in this list.
2. Signed-in inbox/playback, settings, recipient selection, and send need behavioral contracts before their screen visuals can be implementation-ready. Historical assessments are [carousel/playback](../../../v1-assessments/carousel-playback.md) and [recording/send](../../../v1-assessments/recording-send.md).
3. [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) supplies record geometry and sketch intent, but capture/send exceptions and `OPEN-SKP-05` remain. Use the active experience over the v1 recording paint.
4. The physical mute/display-off state has no visible UI to compose. `SCR-EMPTY-HANGOUT` and `SCR-PIN-COOLDOWN` already cover their distinct login states; do not duplicate them here.
5. For future failures, reuse [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) when a centered information overlay is the right surface. Login sends malformed or unclassified server errors to the `connecting` overlay; it does not authorize a more specific error message for those cases.

For each resulting `SCR-*`, enumerate its actual layout and `PANEL-*` mounts, states, transitions, copy authority, `UI-OPEN-###` items, and owner/visual/implementation/verification readiness. Do not infer approval from this inventory.
