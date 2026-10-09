# Feature: Carousel inbox and recipient picker

| Field | Value |
|-------|-------|
| **Status** | **Partial** — server inbox paging **Verified**; carousel/picker on device **Implemented** without automated tests |
| **Areas** | Server, firmware, web twin |
| **Last updated** | 2026-10-08 |

## Purpose

Signed-in users browse async voice messages in a carousel (not a phone list), pick 1:1 or broadcast recipients from the red-circle flow, and keep focus (`last_viewed_seq`) consistent across endpoints.

## UX

- **Carousel:** Center card + peek neighbors; edge navigation; play/pause; visible scrub bar; unread vs read chrome.
- **Hint:** `tap circle to send` once per session (spec).
- **Recipient picker:** Touch targets for hangout members + Everyone; 10 s timeout or shoulder → cancel.
- **Web twin:** Arrow keys for older/newer; `C`/Space for circle; shoulder via `B`/`N`.

## Behavior

- Inbox ordered by server time; **First Message** system row at oldest slot when seeded.
- `PUT /v1/session/view` stores centered `seq`.
- Default inbox API page: 8 messages; `before_seq` retrieves older (firmware does not yet navigate beyond first page).
- Broadcast creates one shared media object and **N−1** inbox rows with shared `broadcast_id`.
- `GET /v1/hangout` supplies roster names for picker.

## Implementation

| Layer | Location |
|-------|----------|
| Inbox API | `server.py` `GET /v1/inbox`, `user_mailbox.inbox_payload` |
| Carousel / picker | `firmware/v1/v1_carousel.c`, `firmware/v1/v1_record.c` (picker UI) |
| Web twin | `demos/server/v1_product/web/box.js` |
| Timing | `shared/v1/timing.yaml` → carousel snap, pick timeout |

## Constraints

- v1 merge: **carousel only** — no fullscreen message detail screen.
- Drawing/photo message kinds reserved; audio is primary `kind`.
- Older `last_viewed_seq` may point outside firmware’s loaded inbox window (no message deletion on server).

## Verification

- Server: `test_inbox_paging_keeps_complete_history`, broadcast in `test_archive.ArchiveTests.test_accepted_broadcast_restart_and_state`.
- Firmware/twin: `make check-v1-parity`; manual carousel navigation per web README.

## Remaining Work

- Firmware inbox **pagination** when `has_more` is true.
- Operational parity: toast duration, offline ribbon copy ([`plans/operational-contract.md`](../plans/operational-contract.md)).
- Autoplay-new profile flag behavior on device if not fully wired.
