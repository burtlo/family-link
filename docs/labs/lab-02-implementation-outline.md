# LAB-02 implementation outline (projected)

Status: **planning only** — no lab firmware in tree yet.  
Lab definition: [inbox plan § LAB-02](inbox-carousel-playback-evaluation.md#lab-02--bounded-inbox-and-card-recycling).  
General layout: [repository layout](repository-layout.md).

This document is the **authoritative file and phase plan** for implementing `LAB-02`. It merges the initial repository-layout sketch with a structured proposal review (agent **Dorabella**, 2026-10-11). Implementation agents should follow this outline unless the owner or architect signs off on a `lab_revision` change.

## Evaluation summary (outline vs proposal)

| Topic | Initial layout doc | Proposal review | Adopted |
|-------|-------------------|-----------------|---------|
| Source layout | Flat `src/*.c` under one project | ESP-IDF `components/lab02/` + `lab02_fixtures` embed + optional `lab02_platform` | **Components** — matches IDF conventions and reuse |
| Platform | Assume `client/shared/platform` exists | Bootstrap has no LVGL; platform starts **in lab** then promotes | **Lab-local platform component** first |
| Variants | CMake flag `LAB02_EAGER_BASELINE` | Kconfig + **separate** `-B` dir for eager builds | **Both** — Kconfig in tree, separate build dir in runbook |
| Verification | Ladder mentioned generically | Scenario IDs (`S-fix-256`, …) mapped to L0–L3 | **Scenario table** below |
| Execution | Manual `idf.py` | + `lab_harness.c`, trace parsers, optional host tests | **Adopted** |
| Evidence | Generic `run-manifest.json` | `family-link.lab-run-manifest/v1` field list + `metrics.json` shape | **Schema doc** [run manifest v1](schemas/run-manifest.v1.md) |
| Log paths | `logs/.../labs/<lab-id>/` | Mixed `LAB-02` vs `lab-02` | **Lowercase slug** `lab-02` everywhere |
| UI spec | Snap aligned with roster panel | Explicit: **message inbox cards** ≠ roster `PANEL-BODY-ROSTER-CAROUSEL` | **Stub message card**; snap geometry only from roster spec |
| Phase B wire | Versioned server fixture | `fl.inbox-page/2` **subset** + lab script hooks | **Subset + scripts**; does not close COM-OPEN items |

## Product scope reminder

- **In scope:** Metadata for 8/16/64/256 summaries; three live card views (16 in eager comparison only); snap/focus; bind generation; stale-content detection; Phase B paging I/O **off gesture path**.
- **Out of scope:** Real audio/sketch decode (LAB-03), full x02 shell (LAB-01), approving inbox paging policy (contract OPEN items).
- **Do not** copy v1 eager sketch buffers, playback task, or full carousel `lv_obj_clean` rebuild.

## Repository tree (target)

```text
client/labs/lab-02-bounded-inbox/
├── README.md
├── manifest.lab.json
├── CMakeLists.txt
├── sdkconfig.defaults
├── partitions.csv
├── Kconfig.projbuild              # sources ../kconfig/lab_common.projbuild; LAB02_* fixture/eager options
├── main/
│   ├── CMakeLists.txt
│   └── app_main.c
├── components/
│   ├── lab02_platform/            # BOX-3 display + LVGL lock (promote → client/shared/platform)
│   ├── lab02_fixtures/            # Embed fixtures/*.json at build time
│   └── lab02/
│       ├── include/lab02/         # Public headers (inbox_model, carousel_controller, …)
│       ├── lab_main.c
│       ├── inbox_model.c
│       ├── inbox_page_metadata.c  # Multi-page indices without N widgets
│       ├── page_provider_local.c  # Phase A
│       ├── page_provider_http.c   # Phase B
│       ├── carousel_controller.c
│       ├── card_pool.c
│       ├── card_view.c
│       ├── card_view_layout.c     # Inbox message stub (not roster COMP-USER-CARD)
│       ├── snap.c
│       ├── instrumentation.c
│       ├── trace_emit.c
│       ├── lab_harness.c          # Scripted scenarios (serial or boot flag)
│       └── eager_carousel.c       # LAB02_EAGER_BASELINE only
├── fixtures/
│   ├── schema.v1.json             # inbox-summary/v1 (lab); align toward fl.inbox-page/2 subset later
│   ├── inbox_8.json … inbox_256.json
│   └── README.md                  # Matrix: dup senders, removed item, corrupt stub, etc.
├── tools/
│   ├── parse_lab_trace.py
│   └── metrics_from_trace.py
└── tests/                         # L1 host or IDF unit tests
    ├── test_inbox_model.c
    ├── test_page_merge.c
    └── test_snap_target.c

server/labs/lab-02-paging/         # Phase B
├── README.md
├── manifest.lab.json
├── pyproject.toml
├── server.py
├── fixture/
│   ├── inbox-summary.v2.json
│   ├── pages/
│   └── scripts/                   # delay, push_during_snap, remove_focused_item, …
└── tests/

docs/labs/evidence/lab-02/
├── REPORT-LAB-02-v1.md            # Published after Phase A close
├── TEMPLATE-REPORT.md             # Copy for new revisions
└── runs/<run-id>/                 # See evidence README + run manifest schema

logs/device/labs/lab-02/<run-id>/
├── serial.txt
└── serial.meta.json
```

## Phase A vs Phase B

| | Phase A | Phase B |
|---|---------|---------|
| **Goal** | Prove bounded widgets + correct bind under local paging | Prove metadata apply during snap, delays, removal, push scripts |
| **Server** | `none` | `server/labs/lab-02-paging` |
| **Version example** | `LAB-02/v1 = firmware <id> + server none + fixture inbox-summary/v1` | `LAB-02/v2 = firmware <id> + server <rev> + fixture inbox-summary/v2` |
| **Firmware** | `page_provider_local.c` slices embedded fixtures | `page_provider_http.c` + async apply queue |
| **Must still** | 64/256 use **metadata pages** in RAM, not 64/256 widgets | No blocking wait in scroll/snap gesture |

Phase A implements multi-page **metadata** locally so 256-item runs are meaningful before HTTP exists.

## Verification scenarios

Map each run to IDs in `run-manifest.json` → `scenarios` and `verification_levels`.

| ID | Description | Phase | Typical level |
|----|-------------|-------|----------------|
| `S-fix-8` | Fixture 8, nominal snap | A | L3 |
| `S-fix-16` | Fixture 16 | A | L3 |
| `S-fix-64` | Fixture 64, local pages | A | L2–L3 |
| `S-fix-256` | Fixture 256, metadata bound | A | L3 |
| `S-trav-20` | 20 end-to-end traversals | A | L2 |
| `S-rev` | Rapid reversal | A | L3 |
| `S-delay` | Injected slow bind / delayed view write | A | L2 |
| `S-eager-16` | Eager 16-widget build, same `inbox_16` | A | L3 (compare to recycle) |
| `S-page-snap` | Page fetch completes during snap | B | L2–L3 |
| `S-rm-focus` | Remove focused item during snap | B | L2–L3 |
| `S-push` | Push / new item script | B | L2–L3 |

Timing, memory, and wrong-content defects require **L3** on hardware. Model/page-merge and snap math may reach **L1** on host.

Link eager comparison runs with `comparison_run_id` in the manifest.

## Execution (summary)

1. **Build (recycle):** `idf.py -C client/labs/lab-02-bounded-inbox -B build/labs/lab-02-bounded-inbox build`
2. **Build (eager):** separate `-B build/labs/lab-02-bounded-inbox-eager` with `LAB02_EAGER_BASELINE` enabled
3. **Boot marker (define in lab README):** e.g. `LAB02_READY build=<id> fixture=<rev> mode=recycle|eager`
4. **Auto-scroll:** `make lab.lab02.scripted` / `CONFIG_LAB_AUTO_HARNESS` for L2 batches; log `HARNESS_DONE`
5. **Phase B:** the canonical `make lab.lab02` and `make lab.lab02.scripted` workflows use managed HTTP by default. The host discovers the current workstation LAN address from `[labs.lab02] client_url = auto` and writes it into the generated kit defaults; use `LABS_LAB02_CLIENT_URL=http://<workstation-ip>:8765` only when auto-discovery is ambiguous. Keep per-kit Wi-Fi in the private deployment roster. The host owns server health, seed, scenario actions, capture, and cleanup. Use `BACKEND=local` only for an explicit local comparison.
6. **Parse:** `tools/parse_lab_trace.py` → `docs/labs/evidence/lab-02/runs/<run-id>/metrics.json`

Full runbook detail: [assistant guide § LAB-02](assistant-guide.md#lab-02-quick-reference).

Do **not** use product `make build` for lab images until `[command.build.lab-02]` exists; that command must not change `[firmware].source` or bootstrap `current.json`.

## Implementation milestones (PR order)

1. Scaffold IDF project, manifests, evidence templates  
2. Platform bring-up (`lab02_platform`, `LAB02_READY`)  
3. Fixture embed + `inbox_model` + `inbox_page_metadata` + L1 tests  
4. Three-view `card_pool` / `card_view` + bind generation  
5. `snap.c` + `carousel_controller` + traces  
6. Instrumentation + trace parsers  
7. Phase A evidence (64/256, S-trav-20, S-rev, S-delay)  
8. Eager baseline + comparison in report  
9. Phase B server + pytest  
10. `page_provider_http` + async apply  
11. Phase B evidence + decision rows (OPEN or recommended)

## Risks (track in report)

- No v2 LVGL platform until lab ports BOX-3 glue.  
- Default **page size** (8 vs 16) is experimental until contract decides — document in fixture README.  
- Focus when item leaves loaded page: lab policy only until product spec exists.  
- Serial trace overhead: note in manifest; optional trace-off sanity run.

## Related

- [Run manifest schema v1](schemas/run-manifest.v1.md)  
- [Metrics JSON shape](schemas/metrics.v1.md)  
- [Evidence templates](evidence/lab-02/TEMPLATE-REPORT.md)
