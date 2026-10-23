# Missing device UI screens

Status: planning inventory, 2026-10-23. Login screens (`SCR-SIGN-IN-ROSTER`, `SCR-EMPTY-HANGOUT`, `SCR-PIN-ENTRY`, `SCR-PIN-COOLDOWN`) are indexed in the [device UI README](../README.md). This file tracks P2-authored signed-in and send-stage screens versus gaps still open. Listed artifacts are draft compositions unless their `SCR-*` doc states otherwise; **no visual approval is claimed here.**

## Not separate screens

| Proposed ID | Role |
|---|---|
| `SCR-CONNECTING.md` | **Do not author.** `connecting` is a lower-slot [`PANEL-STATUS-MESSAGE`](../panels/PANEL-STATUS-MESSAGE.md) overlay on the active stage (`DEC-051`, `EXP-LOGIN` / signed-in experiences). |
| `SCR-WIFI-UNAVAILABLE.md` | **Do not author.** `no Wi-Fi` uses the same lower-slot status panel; keep it distinct from `connecting` in copy and experience rules. |

Connecting and Wi-Fi loss are not full-canvas replacements on inbox, send, or login stages.

## Authored screen compositions (P2)

| Screen | Document | Catalog scenes | Notes |
|---|---|---|---|
| `SCR-MESSAGE-INBOX` | [`SCR-MESSAGE-INBOX.md`](SCR-MESSAGE-INBOX.md) | `CAT-21` … `CAT-26` | Carousel, empty, sketch-on-card, connecting, no Wi-Fi, Pause transport |
| `SCR-RECIPIENT-PICKER` | [`SCR-RECIPIENT-PICKER.md`](SCR-RECIPIENT-PICKER.md) | `CAT-27` … `CAT-30` | Default, multi-select, header overflow, empty roster |
| `SCR-SEND-FEEDBACK` | [`SCR-SEND-FEEDBACK.md`](SCR-SEND-FEEDBACK.md) | `CAT-31` … `CAT-36` | Single screen for preparing → sending → sent, refusal, and send-queue connectivity (replaces a separate progress/result pair) |
| `SCR-VOLUME` | [`SCR-VOLUME.md`](SCR-VOLUME.md) | `CAT-37` | Desk volume slider; hub settings remain out of scope |
| `SCR-RECORD-SKETCH` | [`SCR-RECORD-SKETCH.md`](SCR-RECORD-SKETCH.md) | `CAT-17` … `CAT-19` | Focused-side record canvas + palette; timer/recording indicator off (`OPEN-SKP-05` closed) |

Scene index: [`catalog/README.md`](../catalog/README.md).

## Still missing or deferred

| Proposed document | Gap | Follow-up |
|---|---|---|
| `SCR-SETTINGS.md` | V1 exposed a settings carousel and detail overlays; v2 has volume only (`SCR-VOLUME`). | Author only if the owner retains a settings hub; lock setting set, values, and persistence in an experience spec first. |
| `SCR-SEND-PROGRESS.md` / `SCR-SEND-RESULT.md` | **Superseded** by [`SCR-SEND-FEEDBACK`](SCR-SEND-FEEDBACK.md). | Do not author separate progress/result screen docs unless product reopens the split. |

## Coverage boundary

1. [`EXP-LOGIN`](../../experiences/endpoint-login.md) and signed-in experiences own when lower-slot status messages appear; they are [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) presentations, not entries in the “still missing” table unless a new full stage is approved.
2. Behavioral contracts for carousel, send, and settings remain in [`docs/product/experiences/`](../../experiences/); screen docs own composition and catalog mounts only.
3. [`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) governs record geometry; capture/send exceptions live under message record/send experiences.
4. Physical mute/display-off has no visible UI to compose. Do not duplicate `SCR-EMPTY-HANGOUT` or `SCR-PIN-COOLDOWN` here.
5. For other centered information overlays, reuse [`COMP-STATUS-MESSAGE`](../components/COMP-STATUS-MESSAGE.md) when appropriate.

For each `SCR-*`, enumerate layout and `PANEL-*` mounts, states, transitions, copy authority, `UI-OPEN-###` items, and owner/visual/implementation/verification readiness in that screen’s document—not in this inventory.
