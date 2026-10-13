# LAB-02 session architecture

LAB-02 is a lab implementation of the presentation → session → I/O boundary in
the [client coding standards](../standards/client-application-coding-standards.md).
Its paging and focus fallback policies are experimental, not approved product
behavior.

## Ownership and waits

| Path | Owner | Waits / bound |
|---|---|---|
| Scroll, snap, three card views, status label | LVGL presentation | No network or sleep; one session completion and at most three changed cards per pump |
| Focus ID/index, revision, load stage, page state | `lab02_session` | Single writer; queue operations are nonblocking from presentation |
| Wi-Fi join and HTTP | Boot and fetch workers | Wi-Fi join 15 s; HTTP call timeout 3 s and response-read deadline 12 s; response cap 16 KiB |
| JSON parse | HTTP worker | Parses at most one 16-item page into a bounded typed result |
| Page merge | Session | Copies at most 16 items; success during a drag or snap defers up to 1 s |

The boot worker posts connecting, fetching, and terminal events. The session
checks permitted stage transitions and applies the initial page. A successful
boot starts the fetch worker after the result is acknowledged.

## Fetch operation envelope

Each queued request carries a session generation, monotonically increasing
operation ID, page index, and monotonic submission time. The worker returns one
success, failure, or timeout event. On success the worker copies the parsed page
into the session event queue and releases its fetch slot immediately; the worker
may start the next fetch while a snap defers **application** (not queueing). A
stale session or operation result is rejected. The fetch queue has eight slots;
rejection is counted and traced. Session events use a bounded queue; overflow
increments `fetch_event_dropped`. The session processes at most one completion
per UI pump and retries a missing focused page on a one second cadence. A failed
focused page shows “page unavailable; retrying”. If background page-zero refresh
fails, existing items are labeled “cached”. STA disconnect sets transport
unavailable copy (“reconnecting…”) without erasing the load stage.

The initial fetch uses the same event path. Its failure transitions to an
unavailable stage; automatic boot retry and product copy remain **OPEN**.
Repeated page failures remain visible and retry while that page is focused.

## Inbox revision and focus

The session refreshes page zero after five seconds of no active fetch, drag, or snap.
When a later page announces a newer revision, the session requests page zero
before accepting it. When page zero announces a newer revision, the session
invalidates old pages and outstanding operation IDs, then refetches the focused
page and neighbors. An older revision is discarded. The focused message ID is
used to re-find the card after a shift. If the ID is absent after the focused
page and its neighbors load, the lab falls back to the same clamped index.
That removal fallback remains **OPEN** for the product contract.

## Presentation update levels and resource envelope

The scene is created once. Status changes are Level 0; a changed visible card
window is a Level 1 rebind keyed by message ID, focus, and revision. Routine
120 ms pump wakeups perform no card updates when the session has not changed.
The `S-delay` injection uses a timer and drops stale delayed bindings without
sleeping under the LVGL lock. No Level 2 scene replacement occurs after init.

Design limits: three live cards; at most 256 metadata entries, eight queued
fetches, one active HTTP fetch, a 16 KiB response buffer, and 16 items copied
per page apply. Target measurements for live node count, cJSON transient heap,
largest free block, frame p95/p99, and input-to-focus latency remain **OPEN**
until a representative BOX-3 run. A build or host test does not establish those
measurements.

## Verification

The host suite compiles the actual C page parser and session reducer with test
queue primitives. It checks page replacement, malformed page rejection, result
ownership across two completions, and deferral during a snap. The Python queue
model tests remain supplemental. Phase B still requires scripted device evidence
for delayed HTTP, queue pressure, focused removal, push, repeated traversal,
and allocation/timing budgets. Record L0, L1, L2, and L3 separately in the lab
evidence report.
