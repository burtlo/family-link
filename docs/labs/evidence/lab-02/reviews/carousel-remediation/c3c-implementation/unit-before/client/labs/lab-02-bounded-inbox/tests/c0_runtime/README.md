# C0 carousel baseline runtime

From the repository root:

```sh
python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c0-baseline
```

This builds the actual vendored LVGL software renderer with the production
controller, pool, card, snap, session, metadata and model C files. The display is
320x240 RGB888; software LVGL ticks advance deterministically in 10 ms steps.
The adapter replaces RTOS queues/semaphores with the existing pthread host
support and supplies HTTP page responses directly to the production terminal
producer. Initialization and provider item lookup adapt to the HTTP session;
network transport, hardware input, worker startup and board display are absent.
Gesture displacement uses LVGL's raw scroll function (the same displacement
path used by input), plus begin/scroll/end events; it does not simulate the
hardware pointer driver's velocity or touch arbitration.

Evidence is **L2 headless scripted rendering for these paths**, plus L1 session
checks. It does not close target input, timing, memory or lifecycle findings.
There are no production source changes or remediation claims. The C assertions
intentionally characterize existing failures; replace them with acceptance
checks as each later unit changes the behavior. `characterization=PASS` means
baseline behavior was reproduced, including its defects.

Each scenario runs in a fresh process and starts with 64 synthetic stable IDs,
16 materialized items and focus m4. These deterministic C fixture pages exercise
the same session result/merge paths as HTTP responses; no managed-server receipt
or end-to-end push claim is made. Push appends, matching the lab server's policy.
Removal deletes m8 and uses m9 as the observed next item. The mutation fetch is
submitted before snapping and its response arrives during the animation,
because refresh submission itself is rejected while the snap lock is held.

Eight scenarios preserve the baseline: fast traversal to 20 before release;
unmaterialized focus 20 then page arrival; reversal before snap completion and
programmatic supersession; page arrival during snap; append push during snap;
focused removal during snap; old page result after revision/focus change; and
accepted completion retention under a transport burst. The saved C0 evidence
retains the original lost-terminal reproduction; C1a changes this one scenario
to an acceptance assertion while leaving the other defect expectations intact.

`C0_STATE` records current session/revision/focus ID, snap ID/locks, scroll
position and all three window slots with binding IDs, generations, coordinates,
hidden state and stable fixture sender identifier. Existing production trace
calls are captured synchronously by test support, and start/end counts expose
missing snap terminals. PPM files contain actual LVGL-rendered checkpoint
frames. `pixel-observations.json` records the zero card-background pixels during
fast traversal; the delayed-focus frame visibly retains previous message text
while its binding ID is cleared. The cache path derives from all source/input
hashes and compiler identity; saved command, source-hash and runtime manifests
make the executable state reproducible. Missing dependencies yield an explicit
unittest skip, not a passing runtime claim.

## C2 current acceptance gates

The original C0 artifacts remain in the review evidence. Current delayed-focus
assertions now gate F001: every in-range slot retains its surface, index and
explicit loading/error state, with sender/preview/duration/unread cleared before
reuse. Failed-page tests retain a valid neighboring card and allow traversal to
a newly fetched page; existing focus-driven retry admission remains unchanged.
No tap retry control or retry-policy claim is added by C2. Pool-bounds checks
8/16/64/256 counts, three roots with four children each and unchanged scene/scroll
handles. The original fast traversal/reversal/page-during-snap expectations remain
characterizations of deferred units.

Run the actual production delayed-bind compile branch with:

```sh
python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c2-slow --slow-bind-ms 100
```

This also verifies visible pending surfaces, frozen binding generations, one
supersession terminal per pending bind, and stale reset rejection. Rendered PPM
frames contain exact approved loading/error copy using the existing font plus a
static ellipsis fallback. The compact owner-thread slot snapshot borrows the
materialized item pointer only until the next session pump; pending content is
copied into the existing bounded per-view storage. No full-window/item copy is
placed on the UI stack and no new widget is allocated during reconciliation.
