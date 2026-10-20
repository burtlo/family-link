# LAB-06 — stage shell and panel lifecycle

Status: MODEL and FW implementations are present. ESP-IDF builds are recorded
separately in the FW package result; no target run, visual approval, or measured
performance claim is included. Lab version: `LAB-06/v2`, fixture revision
`lab06-stage-shell/v1`, trace contract `lab06-trace/v1`. Server: `none`.

## Model and source authority

The production C model is
[`components/lab06_model/lab06_model.c`](components/lab06_model/lab06_model.c).
The shell canvas is 320×240. Upper chrome is `(0,0,320,80)` and lower chrome is
`(0,80,320,160)`; their content insets are `(12,12,296,56)` and
`(12,92,296,136)`. The split remains at `y=80`. One upper and one lower panel
belong to the stable shell. A stage change invalidates both panel generations
before their handles are discarded; replacement panels receive fresh
generations. Split-pane geometry comes from `LAYOUT-SPLIT-PANE`; panel ownership
from `PANEL-MODULE-CONTRACT`; the 400 ms cover-down is a motion probe from
`TRANSITION-COVER-DOWN`. Those visual sources are drafts and this lab does not
claim visual approval.

Stages are `roster`, `pin`, `hangout`, `inbox`, `record`, and `receipt`. Stage
transitions have one owner id (the session component). A second writer receives
one `failure/second_writer` result without changing stage. A stage transition
uses a frozen operation id, source, and destination. An operation terminates
once as `success`, `failure`, `cancelled`, `stale`, or `timed_out`; cancellation
returns to the source stage. Async workers, when connected by FW, may only post
immutable event ids to the session owner's queue. They never receive panel
handles or mutate stage state.

Within a session, each newly submitted operation id must be strictly greater
than every previously submitted id. Reused or lower ids are rejected before
state changes and do not produce another terminal result. A pending stage
transition may still complete or cancel with its original id after a later
callback has arrived. The model enforces this contract with a constant-space
watermark; its 128-entry terminal-id ring is bounded diagnostic history, not the
uniqueness check. A new session is required before the 32-bit id sequence wraps.

The event queue capacity is **8** (provisional lab probe). A full queue rejects
the new item, increments `queue_overflows`, and reports one `failure/overflow`;
already queued ids remain intact. A successful enqueue remains pending until
the item is dequeued; dequeue reports its one `success/queue_dequeued` terminal.
Repeated or lower operation ids are rejected by the session watermark without
changing queue state or producing another terminal. The shell owns one reusable transition timer.
`timer_count=1` in the model is a model invariant. Firmware traces independently
count live LVGL timers whose user data belongs to the LAB-06 shell, by walking
LVGL's timer list. H-cycle requires one such timer at baseline and the same
count after every entry and return. Idempotent entry does not allocate another
timer. Cancellation keeps that count unchanged. The declared `cancel_stage` is
`source`.

## Provisional resource envelope

These are model planning bounds only, not measured target budgets or product
limits:

| ResourceBudget field | Provisional lab probe |
|---|---:|
| `max_live_nodes` | 64 |
| `max_transient_bytes` | 4096 |
| `max_persistent_bytes` | 8192 |
| `max_concurrent_operations` | 9 |
| `max_stack_or_call_depth` | 8 |
| `maximum_input_size` | 47 bytes of panel text, plus NUL |

`max_concurrent_operations=9` is the provisional model bound: all 8 accepted
event-queue operations may remain pending until dequeue while the shell also
holds 1 stage transition pending. Synchronous panel-data calls are not pending
after they return. The native `pending_capacity` check holds those 9 accepted
operations simultaneously and verifies that none has terminalized early.

`live_widgets.<stage>=2` (one panel model per slot), and the shared shell timer
count is `1`; these are model expectations to be reconciled with FW's actual
LVGL counters. No heap, LVGL pool, frame-time, or target node budget is approved.

### Firmware panel counters and resource ledgers

The firmware counts the actual LVGL objects under the upper and lower panel
module roots by walking the tree. `live_widgets.<stage>=19` for each stage:
upper root, stage title, status, and previous and next buttons with labels;
lower root, description, synthetic badge/progress/roster label, guide label, and
four action buttons with labels. The scripted H-cycle checks the count after every stage entry and
return across 20 cycles per non-roster stage. At idle, only the two panel trees
are included. During the cover-down overlap the trace reports all live panel
objects, including the retired pair and incoming pair. The single reusable
20 ms shell LVGL timer is counted independently; its expected live count is 1
throughout all transitions and cancellations.

The forward probe moves the incoming stage from `y=+240` to `y=0` over 400 ms.
Returning to the previous stage moves the current foreground from `y=0` to
`y=+240` over 400 ms, revealing the prepared stage beneath it. `Cancel` reverses
the current motion to its source position and restores the source stage.

The lower panel gives the short manual probe with two large primary controls:
tap `Cancel` once, then tap `Exit+Data` once. The first tap arms the probe; the
second starts the transition and applies a bounded update during exit. Watch the
cover reverse automatically at the 200 ms midpoint of its 400 ms forward
motion. Do not tap repeatedly or try to hit a button during the animation. The
upper status reports the terminal result; a successful cancel leaves the shell
in the source stage with `cancelled`. The smaller `Badge` and `Stale` buttons
are optional separate probes. `Badge` demonstrates Level 0 badge and Level 1
roster progress/revision updates. `Stale` delivers a saved generation after
the old panel has been destroyed. `<` and `>` move between the six synthetic
stages. In scripted mode, touchscreen actions are ignored until the harness
finishes so operator input cannot alter its lifecycle checks. Manual captures
may correctly report `INCOMPLETE` because the full decided scenario inventory
belongs to the scripted harness; exploratory interaction is still captured
and parsed. The controls do not authenticate, capture, send, or contact a
server. The default manual capture window is 30 seconds; set `SECONDS=` to
choose a longer window.

The scripted variant runs H-cycle, H-data, H-data-during-exit,
H-level2-control, H-destroyed, H-cancel, then emits H-placeholder as exploratory
with `placeholder=1`. The queue probe fills the actual FreeRTOS queue to its
capacity of eight and records the rejected ninth publication. It does not add
an experiment input or turn H-placeholder into a decided result. The ready
marker is printed only after display, panel, queue, and timer initialization;
the harness completion marker follows all six scenario outcomes. Any failed
scenario emits `LAB06_HARNESS_DONE status=failure`.

Resource reporting keeps two separate ledgers:

| Ledger | Firmware observation | Status |
|---|---|---|
| A — fixed LVGL object pool | `lv_mem_monitor`: total, free, largest free block, and high-water percent; build pool is 65536 bytes | Target values pending device run |
| B — heap_caps memory | Free and largest blocks for internal memory and PSRAM, captured at boot, transition edges, and harness completion | Target values pending device run |

`anim_us` and `build_us` are emitted from device instrumentation. No target was
available for this package, so timing/resource results remain pending and the
performance verdict stays `UNMEASURED`.

Firmware variants use `sdkconfig.defaults.manual` and
`sdkconfig.defaults.scripted`; the scripted symbol is enabled only by the
scripted overlay. Direct ESP-IDF builds use distinct `build-manual` and
`build-scripted` output directories inside this project. The lab uses the
ESP-BOX-3 BSP for the physical display and touch path.

## Host registration and trace evidence

The registered make name is `lab06`, with `manual` as its default variant and
`scripted` as the automated variant. The root make integration builds into
`build/labs/lab-06-stage-shell-manual` and
`build/labs/lab-06-stage-shell-scripted`; both require
`CONFIG_LV_MEM_SIZE=65536`, while only the latter enables
`CONFIG_LAB06_SCRIPTED`. Firmware build ids are `lab06-fw-manual-v3` and
`lab06-fw-scripted-v3`.

`LAB06_READY build=<id> mode=<manual|scripted> server=none
trace_schema=lab06-trace/v1` is accepted only from serial bytes captured after
the adapter arms and resets the selected kit. Scripted capture also requires
exactly one `LAB06_HARNESS_DONE status=complete`. The adapter owns no server or
listener, rejects non-default `BACKEND` values before target resolution, caps
serial evidence at 4 MiB / 4096 bytes per line, redacts URL and credential
forms, and leaves a partial v2 run manifest when setup, capture, parsing, or
cleanup fails.

The lab-owned `tools/parse_lab_trace.py` validates `trace-schema.json`'s
`lab06-trace/v1` JSON-lines contract, operation-id uniqueness and bounds, both
resource ledgers, the exact decided inputs, and the exploratory placeholder.
It writes `family-link.lab-metrics/v2` with source/serial identity, real-sample
nearest-rank distributions, and separate functional and performance verdicts.
`H-placeholder` is always recorded as exploratory with `placeholder=1`; only
`H-cycle`, `H-data`, `H-data-during-exit`, `H-level2-control`, `H-destroyed`,
and `H-cancel` enter functional experiment evaluation. Host fixtures prove
parser and lifecycle logic only; they are not saved as device evidence.

## Wait, ownership, and update map

| Work | Owner | May wait? | Result handling |
|---|---|---|---|
| Input or data callback | Presentation | No | Bounded local event or property update; no I/O |
| Stage transition and stage truth | Session owner | No blocking wait; animation is asynchronous | The owner validates source/writer and commits stage on completion |
| Future network/storage/codec work | Worker | Yes, outside presentation | Immutable event to bounded queue; session owner decides effect |
| Cover-down animation | Presentation timer | 400 ms duration under test; no polling/sleep | Timer callback carries operation id; stale ids are rejected |

| Change | Level | Preserved identity |
|---|---:|---|
| Badge/progress/text property | 0 | Focused synthetic id |
| Roster or bounded panel data | 1 | Focused synthetic id; slot sibling remains mounted |
| Session stage transition | 2 | Shell geometry and transition operation id; panel generations replaced |

Workers never access panel pointers. Before panel destruction the owner marks
the generation dead. A callback with a destroyed generation or during exit is
stale and must perform zero handle uses. Data cannot force Level 2; the
`H-level2-control` deliberately injects that violation and expects failure.

## Scenario inventory

Fixture source: [`fixtures/scenarios.json`](fixtures/scenarios.json). The exact
decided `experiment_inputs` are `H-cycle`, `H-data`, `H-data-during-exit`,
`H-level2-control`, `H-destroyed`, and `H-cancel`. Each operation id is unique
per repetition. `H-placeholder` is exploratory, records `placeholder=1`, and
stays outside experiment inputs. Placeholder screens include connecting/Wi-Fi,
inbox, settings, recipient picker, record chrome, and send feedback; their
product decisions remain OPEN.

| Scenario | Expected terminal and evidence |
|---|---|
| `H-cycle` | 20 cycles per stage; the live LVGL widget count is 19 and the live LAB-06 timer count is 1 after every entry and return |
| `H-data` | Success at Level 0 or 1; focused synthetic id preserved |
| `H-data-during-exit` | One success or stale; old panel text is never visible |
| `H-level2-control` | Negative control fails with `level2_on_data` |
| `H-destroyed` | Stale; zero old-handle uses |
| `H-cancel` | One cancelled terminal; stage `source`; timer count unchanged |
| `H-placeholder` | Exploratory observation only; placeholder bit set |

Every stage transition also emits a `transition_terminal` record with its own
operation id. Setup transitions for `H-data-during-exit` and `H-destroyed`, and
the return half of each H-cycle, remain visible in the trace without entering
the scenario outcome set. The data update during exit and destroyed-generation
callback each emit the sole `scenario_terminal` for their respective scenario.

## Focused native checks

Root discovery compiles the actual production C component and its C test:

```sh
.venv/bin/python -m unittest scripts.tests.test_lab06_model
```

Equivalent direct compile/run (temporary output path shown):

```sh
cc -std=c11 -Wall -Wextra -Werror -pedantic \
  -I client/labs/lab-06-stage-shell/components/lab06_model/include \
  client/labs/lab-06-stage-shell/components/lab06_model/lab06_model.c \
  client/labs/lab-06-stage-shell/tests/model_test.c -o /tmp/lab06_model_test
/tmp/lab06_model_test single_writer
```

Other native scenario selectors are `geometry`, `idempotent_enter`,
`data_during_exit`, `stale_panel`, `cancel`, `level2_control`,
`pending_capacity`, `cycle`, and `overflow`. This MODEL package does not add a
host parser or runner adapter, register `lab06`, run an ESP-IDF build, produce a
measured report, or claim L2/L3 evidence.
