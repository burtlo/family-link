# V1 assessment: carousel and playback

Status: as-built assessment, not a v2 design  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md)  
Primary code: `firmware/v1/v1_carousel.c`, `firmware/v1/x02_main.c`  
Product contract: `docs/BOX-UI.md` § Carousel home

## Purpose and boundary

This subsystem presents the signed-in message inbox, controls focus and snap
animation, downloads and plays audio, synchronizes view/read/position state, and
renders timed sketches. It also paints settings using the same carousel machinery.

The intended boundary is:

- presentation: card tree, snap, header, transport, scrub, sketch invalidation;
- session: focused message, inbox snapshot, play operation, stage;
- I/O: inbox/blob/sketch fetch and view/read/position writes.

V1 does not preserve that boundary consistently. `v1_carousel.c` contains all
three responsibilities and holds direct dependencies on UI, HTTP, codec, NVS,
connectivity, record, and session state.

## User-visible stages

### Browsing

- Entry: successful sign-in or return from settings/record/send.
- Success: one centered message, side peeks, sender header, and transport.
- Interaction: scroll or tap a peek; snap to center; save remote view.
- Never acceptable: focus changes without a matching center card, dead transport,
  or jumping to the first card after a non-stage update.

### Snapping

- Entry: scroll end or non-centered card tap.
- Success: bounded animation, stable logical focus, transport enabled at completion.
- Cancellation: a new snap replaces the prior animation.
- Never acceptable: animation restarting from its own scroll events or network
  latency freezing the animation.

### Playback

- Entry: Play on a stable centered card.
- Work: fetch blob, optionally fetch sketch, open codec, play chunks.
- Success: remain on the same card, preserve final position, mark read, update
  transport in place.
- Failure: as-built only gives an offline toast in some fetch failures; other
  failures may be silent.
- Never acceptable: full scene rebuild at playback completion, UI-thread wait for
  worker shutdown, or applying completion to a newly focused message.

### Inbox refresh

- Entry: WebSocket inbox notification.
- Work: reload complete inbox, retain focus by message sequence where possible.
- Success: new snapshot visible and new-message policy applied.
- Never acceptable: reload blocking presentation, destroying scroll position, or
  using a stale notification for another signed-in user.

## What v1 gets right

- Playback runs on a dedicated task rather than inside the render tick.
- Playback completion requests a transport refresh instead of a full repaint.
- Snap animation deletes a previous animation and suppresses recursive
  `SCROLL_END` handling with `s_scroll_lock`.
- `paint_carousel` preserves focus and scroll when its prior UI tree is still live.
- `v1_carousel_invalidate_ui` clears widget handles after another scene destroys
  the tree, addressing the historical use-after-free/reboot class.
- Inbox notifications are deferred during record and send.
- Carousel paint logs free heap.

## Standards assessment

### Critical — blocking I/O in presentation paths

Violates Standards §§1–2.

- `sync_focus_from_scroll`, `shift_focus_to`, and
  `on_carousel_card_click` synchronously write `/v1/session/view`.
- `v1_carousel_tick_inbox`, called by `ui_task`, synchronously reloads the inbox.
- `play_chirp`, called from `ui_task`, waits for playback stop, sleeps, opens the
  codec, and writes PCM.

These paths explain animation stalls and apparently missed redraws without
requiring an LVGL defect. The presentation owner simply cannot run while waiting.

Required next-version boundary:

- callbacks update local focus immediately and enqueue a coalesced view write;
- inbox reload produces an immutable snapshot event;
- audio cues use a non-blocking audio command queue;
- queue-full behavior is logged and reconciled.

### Critical — playback lacks an operation envelope

Violates Standards §4.

Playback is represented by `s_want_play`, `s_stop_play`, `s_playing`,
`s_play_pos_ms`, and the current mutable focus. `stop_playback` spins until the
worker clears `s_playing`. A focus or session change can occur while fetch or codec
work is in progress, but no immutable operation ID binds completion to the message
that was submitted.

Required structure:

```text
PlayOperation(id, message_id, start_position, session_id)
  -> success(final_position)
  -> failure(reason)
  -> cancelled(reason)
  -> stale(reason)
```

The worker must never reread current focus as operation input.

### High — data refresh uses scene replacement

Violates Standards §5.

After a successful inbox reload, v1 requests a full repaint. `paint_carousel`
calls `lv_obj_clean` and eagerly rebuilds the scene. Scroll preservation reduces
damage but does not make a data refresh a valid scene transition.

The next client should apply an identity-keyed Level 1 collection diff using
message sequence/ID. Level 2 scene replacement should occur only when entering or
leaving the carousel stage.

### High — remote side effects are over-coupled to gestures

Violates Standards §9.

View, read, and position writes happen from several code paths, without an
explicit coalescing or retry policy. Rapid focus changes may produce redundant
writes, and failure is largely ignored.

The next client should define:

- local focus as immediate session truth;
- latest-view write as coalescible;
- position writes as bounded and idempotent;
- read marking as monotonic;
- offline behavior for each write.

### High — full collection and sketch resource growth

Violates Standards §8.

V1 eagerly creates up to `V1_MSG_MAX` cards. Sketch framebuffers are per-card and
large relative to the UI heap. The paint path reports heap but has no enforceable
budget or minimum margin.

The next client should declare:

- maximum live cards (normally center plus bounded neighbors);
- maximum decoded media bytes;
- maximum active sketch buffers;
- scene construction time and memory high-water limits.

### Medium — focus has competing representations

Relates to Standards §§3 and 5.

Logical focus, visual center, snap target, and server `last_viewed_seq` can differ
during a gesture. V1 handles this with `s_scroll_lock` and
`s_carousel_locked`, but the permitted states are implicit.

The next client should model:

```text
FocusState
  settled(message_id)
  moving(from_id, candidate_id, operation_id)
```

Transport is enabled only in `settled`.

### Medium — failure contract is incomplete

Violates Standards §7.

The contract does not fully distinguish blob HTTP failure, codec open failure,
codec write failure, malformed media, cancellation, and stale completion.
Some failures show `can't play right now`; others end silently.

These outcomes require product decisions before v2 implementation. Do not infer
that every failure should use the offline toast.

## Performance priorities

1. Remove HTTP and codec waits from presentation callbacks and ticks.
2. Replace inbox full repaint with message-ID collection diff.
3. Virtualize cards to a bounded visible window.
4. Coalesce view and position synchronization.
5. Measure snap frame time and scene memory at p95/p99, not only free heap after
   construction.

## Reliability tests for the next client

- Snap continues smoothly while the view write is delayed or fails.
- An inbox event during playback does not change the operation’s message ID.
- Playback cancelled during fetch produces one cancelled terminal event.
- A stale playback completion cannot update a newly focused card.
- Inbox diff preserves focus by message ID when items are inserted or removed.
- Scene replacement invalidates all old handles before any refresh callback runs.
- Queue-full injection does not leave transport disabled forever.
- Offline position/read writes follow a documented policy.

## Open product decisions

- Exact user-facing behavior for each playback failure class.
- Whether any blob is guaranteed to remain playable offline.
- Autoplay-new behavior versus the broader “never autoplay on card change” rule.
- Whether scrub remains part of the device product; v1 creates it but hides it.

## Verification level of this assessment

This is source and incident analysis only. It establishes L0/L1 review findings;
it does not claim current device behavior was retested.
