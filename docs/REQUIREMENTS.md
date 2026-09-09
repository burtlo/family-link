# Requirements

Source: the product conversation (split household, Marco Polo access failure, Switch/Minecraft hangouts, Wi-Fi already known). Approved v1 contract: [`plans/v1-product-spec.md`](plans/v1-product-spec.md) (2026-09-04).

## Problem

Two children. Communication with Lynn currently rides on **the other parent’s phone** (Marco Polo, and voice during Minecraft on Nintendo Switches). Kids take turns. Lynn does not have a private channel with each child.

## Goals

Keep a small, ongoing connection:

- A **hangout** (family group) with **users** (Lynn, Mazi, Arlo, …) and **endpoints** (BOX-3 kiosks on desks).
- Any user can sign in on any endpoint. Lynn uses a **box for parity** plus a **web admin** on the home server.
- No cellular. **2.4 GHz Wi-Fi** only.
- Async **audio** mailbox in v1: send 1:1 or broadcast to the hangout.

## Non-goals (v1 merge)

- Not a smartphone, tablet, or app store as the primary child experience.
- Not video. Not live voice hangout in this merge (later).
- Not drawing notes or live shared drawing in this merge (later; high impact when added).
- Not photos in this merge (later).
- Not e-ink, wake word, or canned phrases.
- Not Nintendo Switch Online on the box.

## Functional requirements

### Identity

- **Hangout:** named group; four users (Lynn, Mazi, Arlo, Audrey) and three endpoints in v1.
- **User:** inbox, PIN, profile picture; not bound to one endpoint.
- **Endpoint:** BOX-3 with device token; remembers last signed-in user for wake.
- **Numeric PIN** required for carousel and recording. **1 minute** idle relock to PIN screen (same user portrait). **5 failed attempts** → 60s cooldown.
- **Web admin:** Lynn (granted access) logs in with **web credentials** (separate from box PIN). PIN reset from server only — digits shown on web, not on box glass.

### Async updates (v1 merge: audio only)

| Direction | Media | Notes |
|---|---|---|
| User → user | Audio clip | Long-press circle → recipient (one user or **Everyone**) → record. **5s silence** or **3min** cap stops; long-press stops early. **150ms** leading trim. |
| System → all users | Welcome audio | **First Message** at `seq=1` on user create; Lynn uploads via `/app`; display as `from: Family`. |
| Later | Text, photo, sketch | Out of v1 merge; same inbox carousel model. |

### Carousel inbox

- Center card + peek left/right; datetime order.
- **Read** on Play only (not on card select). Server syncs `read`, `last_viewed_seq`, `position_ms` **per user**.
- Play / Pause and timeline scrub on glass. Volume always visible.
- No autoplay on card change.

### Live hangout (later)

- Half-duplex PTT through server. Not in v1 merge. Island demos (h21) remain reference.

### Group live session (later)

- Server mixing for multiple endpoints. Same PTT discipline.

## Constraints

| Constraint | Decision |
|---|---|
| Radio | Wi-Fi 2.4 GHz only |
| Power | USB wall power; desk appliance |
| Lynn client | BOX-3 endpoint + web `/app` on home Windows server |
| Remote endpoints | Kids’ boxes on other-house Wi-Fi; TLS + Tailscale (or similar) before ship |
| Backend | Server you own; per-user inbox; device token per endpoint |
| Privacy | PIN for mailbox access; mic live only during recording; no wake word |
| Max clip | 3 minutes hard cap; 5s silence auto-stop |

## Success (v1 merge)

Lynn, Mazi, Arlo, and Audrey can each sign in on any endpoint, play and send async voice notes (1:1 or broadcast), with a non-empty mailbox (First Message), PIN privacy, and Lynn admin on the web — **without** borrowing the other parent’s phone.
