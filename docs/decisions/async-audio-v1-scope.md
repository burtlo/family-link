# ADR-001: v1 merge is async audio only

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-09-04 (product spec); indexed 2026-10-08 |
| **Supersedes** | One-box + parent-phone framing in older docs |

## Context

Family Link is a desk **async mailbox** for a hangout (Lynn, Mazi, Arlo, Audrey): sign in on BOX-3 endpoints, browse an inbox, send voice notes 1:1 or broadcast. The problem statement and goals are in [`REQUIREMENTS.md`](../REQUIREMENTS.md). The approved contract is [`plans/v1-product-spec.md`](../plans/v1-product-spec.md).

Island demos (h17–h22, personality, live PTT) prove future paths; they are not the v1 ship list.

## Decision

**v1 merge ships async audio only** for user-generated content:

| In v1 merge | Out of v1 merge (later) |
|-------------|-------------------------|
| Voice clips (record → upload → carousel play) | Live voice hangout / server mixing |
| First Message welcome audio (system) | Drawing / sketch notes as a merge requirement |
| Carousel inbox, PIN, web admin | Photos (child → parent needs dock UVC) |
| Broadcast (`Everyone`) fan-out | Video, cellular, e-ink, canned phrases |
| | Nintendo Switch Online on the box |

**Recording contract (Specification):**

- Tap circle → recipient picker → record immediately.
- Stop: circle tap, **5 s silence**, or **3 min** hard cap; shoulder cancels (discard).
- **150 ms** leading trim after start.
- Mute latch blocks start; near-zero RMS **0.5 s** → abort without send.

**Topology (Specification):** three endpoints, four hangout users; any user may sign in on any endpoint after PIN.

**Also explicitly not in v1 merge:** wake word ([ADR-002](no-wake-word-mic-only-while-recording.md)).

## Consequences

- Firmware product shell (`firmware/v1/`, flash id `x02`) optimizes carousel, record, upload, and sleep UX — not live duplex or photo capture.
- **Observed:** X02 still contains sketch/drawing code paths; product spec treats drawing as out of merge. Do not expand merge scope without revising REQUIREMENTS and this ADR.
- Server work targets multipart WAV ingest and per-user inbox state; chunk/resumable upload is a parallel track ([`features/async-audio-record-send.md`](../features/async-audio-record-send.md)).
- Success criterion: four users can sign in, play, and send async voice **without** borrowing the other parent’s phone ([`REQUIREMENTS.md`](../REQUIREMENTS.md) § Success).

## Evidence and references

- [`REQUIREMENTS.md`](../REQUIREMENTS.md) — goals and non-goals
- [`plans/v1-product-spec.md`](../plans/v1-product-spec.md) — screens, messaging, web `/app`
- [`features/async-audio-record-send.md`](../features/async-audio-record-send.md) — as-built send path
- [`BOX-UI.md`](../BOX-UI.md) — glass contract
