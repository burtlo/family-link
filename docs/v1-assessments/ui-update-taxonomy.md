# Concept guide: UI update taxonomy

Status: explanatory guidance with v1 device examples  
Standard: [Client application coding standards](../standards/client-application-coding-standards.md) §5  
Related assessments: [carousel/playback](carousel-playback.md),
[authentication/connectivity](authentication-connectivity.md), and
[recording/send](recording-send.md)

## Definition

A UI update taxonomy classifies a visual change by how much presentation state it
must destroy and recreate. It prevents the convenient but dangerous response of
rebuilding an entire screen for every state change.

The taxonomy is based on behavior rather than UI framework APIs:

- **Level 0 — property update:** mutate existing presentation properties.
- **Level 1 — subtree update:** reconcile a bounded component or identity-keyed
  collection.
- **Level 2 — scene replacement:** leave one user-visible stage and enter another.

The lowest sufficient level is normally the safest and fastest. Higher levels
invalidate more handles, lose more ephemeral state, allocate more memory, and
create more opportunities for event callbacks to reference destroyed objects.

## Level 0: property update

Use Level 0 when object identity and structure are unchanged.

Examples:

- text or status copy;
- progress, timer, slider, and playback position;
- enabled/clickable state;
- visibility, opacity, color, and icon;
- online/offline badge.

Level 0 should be bounded to one frame budget and should not allocate large object
graphs. It must not perform I/O.

## Level 1: subtree update

Use Level 1 when structure changes inside a stable scene.

Examples:

- adding/removing/reordering inbox cards;
- refreshing one recipient card;
- replacing a panel after data arrives;
- virtualizing center and neighboring carousel cards.

Level 1 requires stable identity keys. Before implementing it, name the preserved
state:

```text
collection identity: message_seq
focus identity: focused_message_seq
ephemeral state: scroll_position, selection, active_operation_id
```

Index alone is rarely a sufficient identity because inserts and removals shift it.

## Level 2: scene replacement

Use Level 2 only when the session stage changes what the user is doing.

Examples:

- roster to PIN;
- PIN to carousel;
- picker to recording;
- recording to send receipt.

Before destroying the old tree:

1. cancel framework animations and timers owned by it;
2. invalidate every stored presentation handle;
3. preserve domain/session state outside widgets;
4. construct the new scene from one immutable session snapshot.

Level 2 must be idempotent for the same snapshot.

## Decision procedure

Ask in order:

1. Did the user-visible session stage change? If yes, Level 2 may be appropriate.
2. Did component membership or structure change? If yes, use Level 1.
3. Otherwise use Level 0.

Network completion, progress, timer ticks, playback completion, and connectivity
badges do not by themselves justify Level 2.

## V1 example 1: carousel transport and playback

### Correct Level 0 employment

`v1_carousel_refresh_transport()` updates focus styling, play/pause icons, count,
slider value, read chrome, sender header, and sketch progress in place. Playback
completion requests `v1_ui_request_transport_refresh()` rather than full repaint.

This is the intended Level 0 path: playback progress does not change the carousel
stage or card collection.

### Concrete improvement

Keep a `CarouselViewModel` snapshot:

```text
focused_id
playing_operation_id
position_ms
duration_ms
transport_enabled
read_state
```

Diff consecutive snapshots and mutate only changed properties. The refresh should
not reread worker globals directly.

## V1 example 2: inbox notification

### Current mismatch

`v1_carousel_tick_inbox()` reloads the inbox and requests a full repaint.
`paint_carousel()` then calls `lv_obj_clean()` and recreates all cards. Scroll and
focus restoration mitigate the damage, but an inbox data refresh is not a session
stage change.

### Correct classification

Inbox refresh is Level 1:

```text
old message IDs -> new message IDs
retain focused message ID when present
insert/remove/update cards
retain scroll anchor and active playback operation
```

If the focused message disappears, the session model—not widget position—chooses
the fallback focus.

## V1 example 3: connecting animation and offline ribbon

### Correct Level 0 employment

`v1_connect_refresh_conn_dots()` changes dot visibility without rebuilding the
connecting scene. `v1_connect_refresh_offline_ribbon()` changes ribbon visibility
while the signed-in stage remains carousel/settings.

These are property updates. A retry finishing does not require replacing the
connecting scene if the stage is still connecting.

### Boundary

A confirmed transition from connecting to roster is Level 2. A probe attempt,
retry countdown, or dot phase is Level 0. Separating these prevents a network
retry from flashing or resetting the screen.

## V1 example 4: send receipt phases

### Current mismatch

The send worker changes `s_send_phase` and requests repaint. `paint_send()` cleans
and rebuilds the whole receipt for `Finishing`, `Sending...`, `Sent`, and
`Couldn't send`.

### Correct classification

- Entering `ST_SEND`: Level 2.
- `Finishing` to `Sending...`: Level 0 text/progress update.
- `Sending...` to terminal receipt: Level 1 if recipient portraits are introduced,
  otherwise Level 0.
- Leaving receipt for carousel: Level 2.

This removes repeated scene allocation and reduces stale-handle exposure while
upload completes.

## V1 example 5: record canvas lifecycle

### Correct Level 0 and Level 2 split

Entering recording creates a full-screen canvas, timer, and record control: Level
2. Pointer movement updates pixels and invalidates the image: Level 0. The elapsed
timer updates one label: Level 0. Leaving recording deletes timers and invalidates
handles before another scene is painted: Level 2 lifecycle cleanup.

### Concrete improvement

The canvas callback should consume a presentation-safe stream of captured points.
Recording operation state remains outside the canvas. Recreating the canvas must
not restart or cancel microphone capture implicitly.

## Common classification errors

- Treating “new data arrived” as “new screen.”
- Treating a full repaint that restores scroll as equivalent to an incremental
  update.
- Using widget existence to decide the session stage.
- Retaining a raw handle after scene destruction and checking it later.
- Rebuilding because a worker has no typed progress event.

## Tests for update-level correctness

- Property updates preserve focus, selection, scroll, and active operations.
- Collection insertion/removal preserves focus by stable ID.
- Repeated rendering of the same snapshot creates no duplicate callbacks/timers.
- Scene exit invalidates every owned handle before queued refresh runs.
- A progress event cannot invoke scene replacement.
- Allocation and frame-time measurements stay within budget for each level.

## Review checklist

- What level is this update, and why is a lower level insufficient?
- If Level 1, what identity and ephemeral state are preserved?
- If Level 2, which session transition caused it?
- Which handles, animations, and timers are invalidated?
- Can the scene be reconstructed only from the session snapshot?
