# LAB-02 — Bounded inbox and card recycling (Phase A)

ESP-IDF lab firmware for **four** reusable message card views, local or HTTP paging, horizontal snap, and bind generations.

## Boot marker

After display init, serial prints:

```text
LAB02_READY build=<app version> fixture=<inbox_N> mode=recycle|eager
```

## Build and run (make)

Registered as **`lab02`** in [`config/labs.registry.yaml`](../../../config/labs.registry.yaml).

```powershell
make lab.help
make lab.lab02.build
make lab.lab02 KIT=lynn                    # manual: you scroll during capture
make lab.lab02.scripted KIT=lynn           # auto-scroll on device
make lab.lab02 BACKEND=local KIT=lynn       # explicit local-fixture comparison
```

The two canonical commands default to managed HTTP paging. The host owns fixture
server startup, health, seed, capture, and cleanup. Manual traversal permits
loading cards; scripted mode requires no operator touch and a `HARNESS_DONE`
marker. Each run announces capture activation before traversal begins.

Fixture size: menuconfig **LAB-02 Bounded Inbox → Embedded inbox fixture size** (default 16), then rebuild.

### Manual idf.py (optional)

```powershell
idf.py -C client/labs/lab-02-bounded-inbox -B build/labs/lab-02-bounded-inbox build
idf.py -C client/labs/lab-02-bounded-inbox -B build/labs/lab-02-bounded-inbox flash monitor
```

The eager comparison is an internal local-fixture control: use `make lab.lab02 BACKEND=local VARIANT=eager KIT=lynn` when comparison evidence requires it.

## Trace (`lab-trace/v1`)

Lines prefixed with `LAB_TRACE`:

```text
LAB_TRACE t_us=<µs> event=<name> gen=<u> focus=<i> msg=<id> [extra]
```

Events include `bind`, `unbind`, `snap_start`, `snap_end`, `harness_done`.

Parse capture:

```powershell
python client/labs/lab-02-bounded-inbox/tools/parse_lab_trace.py logs/device/labs/lab-02/<run-id>/serial.txt
```

## Harness

Auto-scroll: `make lab.lab02.scripted` (sets shared **`CONFIG_LAB_AUTO_HARNESS`**). menuconfig: **Family Link device lab** → Auto-scroll inbox after boot. Ends with `HARNESS_DONE` on serial.

See [evidence STATUS](../../../docs/labs/evidence/lab-02/STATUS.md) for the Phase A checklist.

## Current status (Phase A)

| Area | Status |
|------|--------|
| Local fixtures 8/16/64/256 embedded | Done |
| Metadata page model (no N widgets for large fixtures) | Done |
| Recycle carousel (4 views) + snap | Done — touch scroll on hardware |
| Bind generation + stale drop counter | Done |
| Eager 16-widget path | Done (Kconfig) |
| `page_provider_http` | Phase B (`fl.inbox-page/2`, async apply off snap) |
| L1 host tests | `make test` (`scripts/tests/test_lab02_host.py`) |
| `S-delay` slow bind | Kconfig `LAB02_SLOW_BIND_MS` |
| Device capture | `make lab.lab02` / `scripts/lab_host.py` |
| Managed Phase B server | `server/labs/lab-02-paging/`, owned by canonical runs |

## Manifest

See [`manifest.lab.json`](manifest.lab.json). Logical version example:

`LAB-02/v1 = firmware <build-id> + server <run-owned revision> + fixture inbox-summary/v1`

## Related docs

- [Implementation outline](../../../docs/labs/lab-02-implementation-outline.md)
- [Assistant guide](../../../docs/labs/assistant-guide.md)

C3a page-arrival contract: ordinary pages apply during drag/snap without moving
the viewport; newer page-zero mutations terminalize their fetch and retain one
bounded immutable snapshot until motion ends. Applied and pending revisions are
separate. See [session architecture](../../../docs/labs/lab-02-session-architecture.md)
and [owner decisions](../../../docs/labs/evidence/lab-02/reviews/carousel-remediation/execution.md).
C3b continuously covers the viewport with four roots (20 card nodes including
four children per root), independent of fixture size. Geometry remains 320/132/12;
[APP-O-05](../../../docs/labs/evidence/lab-02/reviews/carousel-remediation/execution.md)
authorizes four views. [Carousel requirements](../../../docs/labs/lab-02-carousel-remediation-plan.md)
and [coding standard](../../../docs/standards/client-application-coding-standards.md)
govern this path. Focus intents use one latest slot and a 20 ms engineering
submission interval (50/s plus immediate gesture-end flush), not a product timing
gate. Unadmitted focus fetches retry admission at most once per 20 ms, stop when
admitted/materialized/failed/superseded, and never allocate another intent queue.
Input updates four bounded roots, retaining overlapping identities and delayed
bind timers. Snap target intent stays owned by the session during animation while
visible roots follow actual scroll geometry. F003 snap supersession is verified at
L1/L2; F006 owner tap-to-retry and initial inbox retry are implemented with L1/L2
gates. Target memory/input/timing measurements and
numeric product gates remain OPEN for C5/C6.

C3c snap supersession uses one bounded presentation-owned operation envelope.
Every acceptance, including no-motion, emits `snap_start` then exactly one
`snap_end` with its frozen ID/session/revision/focus and normalized outcome/reason.
Real reversal terminalizes the old snap as stale; animation-generated begin/end
events are suppressed only during the synchronous tick. Pending mutation
reconciliation captures replacement focus before unlock and computes the new
target from the reconciled owner snapshot. C3c host pointer-driver/lifecycle
checks are L2 self-checks; physical touch/timing/stack closure remains L3 pending.
