# Remaining view-controller slices

Date: 2026-10-30
Status: Planning documents for review; no implementation started by this update.
Baseline: owner reports Slice 2 delivered and verified.

## Delivery sequence

| Slice | User-visible outcome | Definition |
|---|---|---|
| 3 | Play, pause, resume and replay audio with synchronized sketch inside the selected Inbox card | [In-card playback](device-ui-view-controller-third-slice-playback.md) |
| 4 | Record audio and multicolor sketch, send durably to selected people, and recover queued delivery | [Recording and delivery](device-ui-view-controller-fourth-slice-record-send.md) |

Slice 3 can use existing server messages without new recording. Slice 4 uses
Slice 3 to verify that a newly sent message is playable by its recipients.
Shared storage and surface abstractions introduced in Slice 3 should support
Slice 4 without reserving a record canvas before recording needs it.

The current runtime has typed recording navigation with copied recipients and
resolves it to `NoDestinationViewController`. Inbox browsing has a metadata-only
service and three reusable card widgets. These are the starting seams; do not
restore the legacy global feature presenter or eager attach bundle.

## Decisions carried forward

- Root owns child appearance and transition initiation. Base lifecycle methods
  dispatch callbacks through appearance brackets.
- One live controller tree; static snapshots preserve specified transition
  visuals. Stage and lower-slot motion follow their existing specifications.
- Playback remains in the centered Inbox card.
- One selected-card live sketch surface; no permanent uncompressed image per
  message. State survives through stroke/time data and bounded stored snapshots
  or reconstruction.
- The recording canvas is a lazy 240×240 RGB565 lease, not a boot reservation.
- Audio and sketch streams use persistent local storage and bounded working
  buffers. Network stalls grow durable backlog, not RAM queues.
- Keep the initial 32 KiB LVGL pool. Measure replacement controllers and media
  operations as they are added; propose any increase with concrete evidence.
- Preserve boot hardware ordering, health, credential refresh, Volume, mute,
  and existing signed-in navigation.
- Keep unfinished capture distinct from committed outbox work. Mute/reboot
  outcomes follow the recording/send experience.
- No extra recording timer/indicator, separate playback screen, profile editor,
  settings hub, or storage-health boot project is introduced by these slices.

## Evidence and qualification

Each package receives implementation tests and an independent read-only review.
Controller tests use pinned real LVGL and the product pool/font configuration.
Worker/service tests cover stale results, queue pressure, bounded waits,
quiescence and allocation failures. Target runs record exact build identity,
serial, capability-specific memory and interaction timing.

Reuse LAB-03/LAB-04/LAB-10 media evidence and LAB-16 storage/upload probes as
relevant engineering evidence. A lab PASS does not establish product behavior.
Do not re-qualify the rejected eager screens as a prerequisite.

## Planning versus implementation readiness

These documents define outcomes and assignable packages. Before dispatching a
package, read the current owning code and active experience/communication
contracts, freeze its brief and tests, and record any unresolved wire or format
decision that actually blocks it. Do not silently implement a proposed wire
field or turn every remaining engineering measurement into a product interview.

Slice 3 leaves the recording stand-in in place. Slice 4 removes it when the
real capture and delivery path is ready. Final acceptance crosses both slices:
select recipients → record audio/sketch → receive truthful `sent` → recipient
plays the matching audio and drawing inside a card.
