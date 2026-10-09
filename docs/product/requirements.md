# Product requirements (v1 merge)

**Authoritative contract:** [`plans/v1-product-spec.md`](../plans/v1-product-spec.md) (2026-09-04). This document summarizes the same product truth for navigation; if they disagree, the spec wins until reconciled.

Source conversation: split household, shared-phone access, desk hangout with Lynn, Mazi, Arlo, and Audrey.

## Problem

Two children. Communication with Lynn currently rides on **the other parent’s phone**. Lynn does not have a private channel with each child.

## Goals

- A **hangout** with **users** and **endpoints** (BOX-3 kiosks).
- Any user can sign in on any endpoint; Lynn uses a **box for parity** plus **web admin** on the home server.
- No cellular. **2.4 GHz Wi‑Fi** only.
- Async **audio** mailbox in v1: 1:1 or broadcast.

## Non-goals (v1 merge)

- Not smartphone-primary for children.
- Not video, live voice hangout, drawing notes, or photos in this merge.
- Not e-ink, wake word, or canned phrases.
- Not Nintendo Switch Online on the box.

## Functional requirements (summary)

### Identity

- Hangout: four users, three endpoints in v1.
- Per-user PIN gates carousel and recording; **1 min** idle relock; **5** failures → **60 s** cooldown.
- Web admin: separate credentials; PIN reset on server only (digits not shown on glass).

### Async audio

- Long-press record flow; **Everyone** or one user; **5 s** silence or **3 min** cap; **150 ms** leading trim.
- System **First Message** at `seq=1` when a user is created.

### Carousel inbox

- Datetime order; read on **Play** only; server syncs read, position, and `last_viewed_seq` per user.
- No autoplay on card change.

## Constraints

| Constraint | Decision |
|---|---|
| Radio | Wi-Fi 2.4 GHz only |
| Power | USB wall power |
| Lynn client | BOX-3 + web `/app` on home server |
| Remote endpoints | TLS + Tailscale (or similar) before ship |
| Privacy | Mic live only during recording; no wake word |

## Success (v1 merge)

Each person can sign in on any endpoint, play and send async voice (1:1 or broadcast), with First Message, PIN privacy, and Lynn admin on the web — without borrowing the other parent’s phone.

## Related

- Vision narrative: [`vision.md`](vision.md)
- Endpoint UX detail: [`../BOX-UI.md`](../BOX-UI.md)
- Legacy path (redirect): [`../REQUIREMENTS.md`](../REQUIREMENTS.md)
