# LAB-02 — Bounded inbox and card recycling (Phase A)

ESP-IDF lab firmware for **three** reusable message card views, local or HTTP paging, horizontal snap, and bind generations.

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
make lab.lab02.eager KIT=lynn              # eager 16-widget comparison
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

Eager build: `make lab.lab02.eager.build` or `-B build/labs/lab-02-bounded-inbox-eager` with `sdkconfig.defaults.eager`.

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
| Recycle carousel (3 views) + snap | Done — touch scroll on hardware |
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

- [Implementation outline](../../docs/labs/lab-02-implementation-outline.md)
- [Assistant guide](../../docs/labs/assistant-guide.md)
