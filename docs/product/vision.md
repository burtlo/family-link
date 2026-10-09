# Family Link — product vision

Family Link is a **desk appliance** for a small hangout (family group): each person can sign in on a shared **ESP32-S3-BOX-3** endpoint and use an **async audio mailbox** without borrowing someone else’s phone.

## Problem

When communication rides on another parent’s phone (voice during games, async apps on a shared device), Lynn does not have a private, always-available channel with each child.

## Experience (product-centered)

The product is defined by what people do on the box, not by server layout:

1. **Choose who you are** on the endpoint roster.
2. **Unlock** with a personal PIN (privacy on a shared desk).
3. **Browse** the inbox carousel (peek left/right; play when ready).
4. **Record** a voice note: pick one recipient or **Everyone**, speak, send.
5. **See honest feedback** that the message was accepted or that something failed.
6. **Play back** audio with scrub and volume; read state syncs per user.
7. **Recover** from Wi‑Fi drops, server restarts, and (where implemented) partial uploads.

Lynn also uses a **web admin** on the home server for PIN reset, welcome audio, and sending without flashing firmware.

## v1 merge scope (current contract)

Approved contract: [`plans/v1-product-spec.md`](../plans/v1-product-spec.md). Detailed requirements: [`requirements.md`](requirements.md).

**In v1 merge:** async **audio** only; four users; three endpoints; PIN-gated carousel; web admin; server you own; 2.4 GHz Wi‑Fi.

**Later (not v1 merge):** live voice hangout, drawing/sketch notes, photos, video, cellular, wake word, e‑ink, canned phrases.

## What this is not

Not the earlier **e-ink canned-phrase** messenger or **pocket cellular/Gmail** experiments — this repo is a Wi‑Fi desk BOX with mics and a speaker. Reuse at most: a server you own and per-device identity.

The repo name `family-link` is a placeholder ([`../NAMES.md`](../NAMES.md)).

## As-built behavior

Shipped behavior is documented under [`../features/README.md`](../features/README.md). That index is **implementation truth**, not a promise for the next product generation.
