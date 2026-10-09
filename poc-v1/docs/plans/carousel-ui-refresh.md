# Plan: carousel UI refresh + shoulder settings

| Field | Value |
|---|---|
| **Status** | in progress (2026-09-05) — firmware + web twin shipped; `/app` gallery phase 2 |
| **Firmware** | `x02_product_shell.c` |
| **Server** | `PUT /v1/profile`, profiles on hangout + login |
| **UI brief** | [`BOX-UI.md`](../BOX-UI.md) |

Approved in design session 2026-09-05. Implements ribbons, peek redesign, short-press record, scrollable shoulder settings, and per-user avatar + accent.

---

## Canvas

- **320×240** RGB565 landscape (unchanged).
- **Top ribbon (20px):** `#0C0E10`. Right: message index `3/7`. Left reserved for mute + Wi‑Fi dots (empty v1).
- **Bottom ribbon (20px):** `#141A1E`. Center: toast when active, else `shoulder = back`.
- **Content band:** ~200px between ribbons. Card + peeks with **8px** margin to ribbons.

## Carousel

- **Center card:** 200×~184 (pane ~162 + 22px track). Sender **avatar** + **accent** on face pane; lightened tint on play pane.
- **Play:** 64px hit target, ~40px filled disk, yellow play/pause icon.
- **Peeks (56px):** `#242C34`, cropped face band + 32px portrait + sender name `#E8F0E8`. Unread: 2px gold on outer edge.
- **Unread center card:** 2px gold border.
- **Read rule:** unchanged — Play marks read.

## Red circle (physical)

| State | Short tap | Shoulder |
|---|---|---|
| Asleep | Wake | — |
| Carousel | Recipient picker | Settings |
| Picker | — | Cancel |
| Recording | Stop + upload | Cancel discard |
| Playing/scrubbing | Ignored | Settings |

- 400ms debounce after carousel paint.
- First login toast once: `tap circle to send`.
- Recording overlay: `listening...` + `tap circle · shoulder cancel`.
- No fake red button drawn on LCD.

## Shoulder settings (scrollable)

Order: **name → volume → color → face → sign out**.

- Volume: slider width 200 (ends x=256), numeric 78–100 at x=268.
- Color: 10 swatches, 2×5 grid, 40×40 taps.
- Face: slot 0 geometry + 12 built-in fun avatars (48×48 RGB565, packed in flash).
- Sign out: bottom of scroll, single tap.
- Shoulder exits settings anytime.

## Identity model

Per **`user_id`** on server:

```json
{ "avatar_slot": 0, "accent_hex": "#5AA0E8" }
```

- `avatar_slot` 0 = geometry default; 1–12 = built-in creature sprites.
- Sender avatar + accent on message cards and peeks.
- Self avatar + accent on roster, PIN, settings.
- `PUT /v1/profile` from box; profiles included in login + `GET /v1/hangout`.
- `/app` upload gallery: **phase 2**. No real photos in v1.

## Build phases

| Phase | Status |
|---|---|
| 1 — Layout + short-press circle | done in firmware |
| 2 — Profile API + shoulder UI + avatars | done |
| 3 — Web twin parity | done (`demos/server/v1_product/web/`) |

## Assets

- `scripts/gen_avatars.py` → `firmware/assets/avatars/avatars.h`
- Replace placeholder creature art by re-running the script after dropping new PNGs (future).
