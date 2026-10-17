# Lab evidence

Store **reports and run metadata** here. Large binaries (screen recordings, full serial dumps) may live under `logs/device/labs/` or outside git; reference them from the run manifest with paths and hashes. Shared `.run` commands use the [run manifest v2](../schemas/run-manifest.v2.md) and [metrics v2](../schemas/metrics.v2.md) contracts. Older LAB-02 artifacts may use the [run manifest v1](../schemas/run-manifest.v1.md) format.

## Layout

Use **lowercase lab slug** for evidence folders (`lab-02`, not `LAB-02`). Serial logs use the same slug under `logs/device/labs/`.

```text
docs/labs/evidence/
└── <lab-slug>/                  # e.g. lab-02
    ├── REPORT-<LAB-ID>-<rev>.md
    ├── TEMPLATE-REPORT.md       # Optional per-lab copy (LAB-02 has one)
    └── runs/
        └── <run-id>/
            ├── run-manifest.json   # Shared runs: schemas/run-manifest.v2.md; legacy LAB-02: v1
            ├── events.jsonl        # Shared run stage/action event journal
            ├── metrics.json        # When produced; see schemas/metrics.v2.md or legacy v1
            ├── notes.md
            └── video.md
```

Shared-run IDs are generated as `YYYYMMDD-<variant>-run-<8 hex>` unless overridden with `RUN_ID=`. Older/manual report IDs may use the descriptive `YYYYMMDD-<phase>-<fixture>-<variant>-<seq>` convention (e.g. `20261011-phase-a-64-recycle-01`).

## Shared run evidence examples

The accepted P4 LAB-02 S-push integration runs have `workflow_status=PASS` and
`experiment_verdict=PASS` for both modes. See the [manual
manifest](lab-02/runs/20261017-p4-spush-manual-57c53bac-r2/run-manifest.json)
and [scripted
manifest](lab-02/runs/20261017-p4-spush-scripted-e4c1ac32-r3/run-manifest.json).

The P5 LAB-05 no-server integration proof uses the generic adapter with no
`workflow` settings. Its manifests record both `service` and `scenario` as
`not_configured`; each workflow passed, while the experiment verdict is
`UNMEASURED` and no performance result is claimed.

| Mode | Manifest | Event journal | Serial reference |
|------|----------|---------------|------------------|
| Manual | [run-manifest.json](lab-05/runs/20261017-manual-run-fd3cc46d/run-manifest.json) | [events.jsonl](lab-05/runs/20261017-manual-run-fd3cc46d/events.jsonl) | `logs/device/labs/lab-05/20261017-manual-run-fd3cc46d/serial.txt` |
| Scripted | [run-manifest.json](lab-05/runs/20261017-scripted-run-5a9c9dd1/run-manifest.json) | [events.jsonl](lab-05/runs/20261017-scripted-run-5a9c9dd1/events.jsonl) | `logs/device/labs/lab-05/20261017-scripted-run-5a9c9dd1/serial.txt` |

The scripted serial capture does not contain `LAB05_HARNESS_DONE`, so its
workflow `PASS` means the configured host lifecycle completed; it does not
prove scripted harness completion. Both runs have no metrics artifact.

## Report template

LAB-02: copy [lab-02/TEMPLATE-REPORT.md](lab-02/TEMPLATE-REPORT.md). Other labs: create `REPORT-LAB-XX-v1.md` with:

1. **Lab version** — `LAB-02/v1 = firmware … + server … + fixture …`
2. **Hardware** — board, display config, IDF version
3. **Scenarios** — bullet list of what was exercised
4. **Target** — gates chosen before the run
5. **Measured** — tables or links to `metrics.json`
6. **Triggers** — which review triggers from the plan fired (if any)
7. **Decision** — recommended product/contract/UI change, or **OPEN** with evidence link
8. **Verification** — L0–L3 per scenario

Do not commit secrets or household data.

## Index

| Lab | Reports |
|-----|---------|
| `LAB-01` | *(none yet)* |
| `LAB-02` | [STATUS.md](lab-02/STATUS.md) (checklist; reports when published); see existing run evidence under `lab-02/runs/` |
| `LAB-05` | [Manual run](lab-05/runs/20261017-manual-run-fd3cc46d/run-manifest.json) · [scripted run](lab-05/runs/20261017-scripted-run-5a9c9dd1/run-manifest.json) |
| … | Add rows when reports land |

Update this table when publishing a report.
