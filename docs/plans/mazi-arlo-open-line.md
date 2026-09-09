# Plan: Mazi and Arlo open line

| Field                          | Value                                                                 |
|--------------------------------|-----------------------------------------------------------------------|
| **Doc kind**                   | `feature-plan`                                                        |
| **Owners / areas**             | Device firmware, host protocol, two-box desk                          |
| **Status**                     | `active`                                                              |
| **Targets**                    | Two BOX-3 kits as friends (Mazi ↔ Arlo), not the parent-phone v1 path |
| **Last updated**               | See git history                                                       |
| **Supersedes / superseded by** | None                                                                  |
| **As-built**                   | None — link to [`docs/features/`](../features/_template.md) when shipped |

## At a glance

Two desk boxes pretend to belong to Mazi and Arlo. The top mute latch is how each person says “I am open to talking” or “I am away.” The server in the middle remembers that flag and tells the other box. A second demo streams live voice while they are open. A third demo writes a dated diary of everything said while the box is unmuted. Live shared drawing and timed drawing notes are **high impact** (desk 2026-08-28).

These are **island demos** (one job per flash). They do not change the product hangout, which stays half-duplex hold-to-talk with you on a phone ([`REQUIREMENTS.md`](../REQUIREMENTS.md)).

| Phase | Outcome | Status |
|------------------------------------------------------------|------------------------------|--------|
| [Phase 1 — Mute as open/away](#phase-1--mute-as-openaway) | Each box heartbeats its mute state; the response carries the friend’s last known state | `done` |
| [Phase 2 — Live voice while open](#phase-2--live-voice-while-open) | Unmute streams PCM through the server so both can talk | `done` (desk success 2026-08-24) |
| [Phase 3 — Diary while unmuted](#phase-3--diary-while-unmuted) | Unmute records continuously; the server stores dated chunks; mute stops it | `done` |
| [Phase 4 — Live shared drawing](#phase-4--live-shared-drawing) | Finger on one glass, ink on the other | `done` (desk success 2026-08-28, **high impact**) |
| [Phase 5 — Drawing note](#phase-5--drawing-note) | Record a timed sketch, send, replay at the same speed | `done` (desk success 2026-08-28, **high impact**) |

---

## Background

The BOX-3 mute key is a **hardware latch**. While it is down, analog mics are dead (`BSP_MUTE_STATUS` / GPIO1, active-low). That is the opposite of product PTT (h04 / h11), which tried to treat mute as hold-to-talk and recorded silence.

These demos lean into the latch:

| Mute key | Mics | Heartbeat | Talk / diary |
|---|---|---|---|
| Down (red LED on) | Hardware-muted | `available: false` (away) | Do not send audio |
| Up (red LED off) | Live | `available: true` (open) | Stream / record |

Both boxes still heartbeat while away. Away is not offline. Offline is “no heartbeat for ~10 s.”

Protocol ids stay `box-a` / `box-b` (tokens unchanged). Registry **names** are Mazi and Arlo so the screens can say the people’s names. Flash Mazi’s kit as `box-a`, Arlo’s as `box-b`. Same firmware, different `firmware/secrets.h`. There is **no** box-to-box Wi-Fi; both talk to this Mac.

**Related docs:** [`TWO-BOX.md`](../TWO-BOX.md), [`DEMO-MAP.md`](../DEMO-MAP.md), [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md), [`SERVER-DEMOS.md`](../SERVER-DEMOS.md).

---

## Phase 1 — Mute as open/away

**Goal.** Mazi’s screen shows whether Arlo is open or away, and Arlo’s screen shows Mazi, using only request/response heartbeats.

**Deliverables**

- Host `demos/server/h20_presence/` — `POST /v1/heartbeat` body `{available: bool}` records that device’s state. Response includes `self` and `peer` (`id`, `name`, `online`, `available`, `updated_at`).
- A status change overwrites the recorded state. The friend’s **next** heartbeat response contains it. No WebSocket required.
- Firmware **h20**: poll the mute latch, POST on change and about every 1 s, paint you + friend.
- Smoke: `make demo-presence` (two Python twins, no box).

**Acceptance**

- Mazi heartbeats `available: true`; Arlo’s next heartbeat sees `peer.available: true` and `peer.name: Mazi`.
- Mazi switches to muted; Arlo’s following heartbeat sees `peer.available: false`.
- Kit: `-- PASS h20` after a 200 that includes a peer object. Mute and the friend’s line should match.

**Status:** `done`

---

## Phase 2 — Live voice while open

**Goal.** Both kits start muted. Unmuting streams that person’s microphone through the server to the other speaker so they can have a real-time voice conversation.

**Deliverables**

- Host `demos/server/h21_talk/` — WebSocket `/v1/ws`, hello, then **copy-through both ways** (no invite, no floor). Binary frames are 16 kHz s16le mono, 640 bytes / 20 ms.
- Firmware **h21**: start with whatever the latch is (operators should boot muted). Unmute → send PCM. Mute → stop sending. Play incoming audio whenever frames arrive so you can hear the friend while you are muted. JSON `status` / `peer_status` carries the friend’s mute latch. Speaker uses h18 ROOMVOL (mute, 78 … 100 step 2).
- Smoke: `make demo-talk` (two twins send, each receives the other’s bytes).

**Acceptance**

- `-- PASS h21` after about 10 frames sent and 10 received (same bar as h11).
- Two kits in **different rooms** (or one kit + the smoke path): speech is intelligible. Same-desk full duplex will echo — that is expected, not a fail.

**Status:** `done` — **desk success 2026-08-24.** Two BOX-3 kits (Mazi ↔ Arlo) talk through the Mac host: mute cards follow the friend, voice crosses both ways, ROOMVOL works. Next: same-time two-way tryout with Audrey as the other end (two rooms; expect echo if both unmuted in one room).

---

## Phase 3 — Diary while unmuted

**Goal.** While the box is open it records continuously and uploads chunks. The server keeps each chunk with a datetime. Muting stops the upload.

**Deliverables**

- Host `demos/server/h22_diary/` — `POST /v1/diary` multipart (`session`, `seq`, `blob` WAV). Server stamps `received_at` (UTC) and writes under `data/h22_diary/`. `GET /v1/diary` lists that device’s chunks.
- Firmware **h22**: unmute opens the mic once for the session (avoid a click every second), fills ~1 s WAV chunks, POSTs them, closes the mic on mute.
- Smoke: `make demo-diary`.

**Acceptance**

- Two POSTs as Mazi appear in GET with distinct `received_at` values and playable WAV bytes.
- Kit: `-- PASS h22` after the first 200 upload. Muting stops new files; unmuting starts a new `session`.

**Status:** `done`

---

## Phase 4 — Live shared drawing

**Goal.** Draw on one glass; the same stroke appears on the other in real time.

**Deliverables**

- Host `demos/server/h26_draw/` — WebSocket `/v1/ws`, hello, then copy `stroke` / `clear` to the peer.
- Firmware **h26**: RGB565 canvas, local white ink, friend blue, Boot clears both.
- Smoke: `make demo-draw`.

**Acceptance**

- `-- PASS h26` after the first inbound stroke. Heart drawn on Mazi appears on Arlo.

**Status:** `done` — **desk success 2026-08-28. High impact.**

---

## Phase 5 — Drawing note

**Goal.** Record a drawing with timestamps, send it, open it later, replay at the same speed.

**Deliverables**

- Host `demos/server/h27_sketch/` — `POST /v1/sketches` timed stroke blob, inbox GET, blob GET, mark read.
- Firmware **h27**: home with Draw + New message, Boot sends, Boot stops playback (unread until it finishes).
- Smoke: `make demo-sketch`.

**Acceptance**

- `-- PASS h27` on send or first inbound point. Friend can be away when it is sent.

**Status:** `done` — **desk success 2026-08-28. High impact.**

---

## Open questions

1. If this mapping feels right on the desk, does product hangout stay hold-to-talk (h11 / x01), or does mute-as-open replace it for kid-to-kid only?
2. Full-duplex talk (phase 2) vs product half-duplex: keep both as separate demos unless a later plan picks one.
3. Diary retention / who can play the journal back on the box is out of scope here.

---

## References

- Firmware: `firmware/demos/h20_presence.c`, `h21_talk.c`, `h22_diary.c`, `h26_draw.c`, `h27_sketch.c`
- Hosts: `demos/server/h20_presence/`, `h21_talk/`, `h22_diary/`, `h26_draw/`, `h27_sketch/`
- Make: `make h20` … `h22`, `h26`, `h27`; `make demo-presence`, `make demo-talk`, `make demo-diary`, `make demo-draw`, `make demo-sketch`
- Mute GPIO: [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md) h08 / h17 notes
