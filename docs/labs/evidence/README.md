# Lab evidence

Store **reports and run metadata** here. Large binaries (screen recordings, full serial dumps) may live under `logs/device/labs/` or outside git; reference them from `run-manifest.json` with paths and hashes.

## Layout

Use **lowercase lab slug** for evidence folders (`lab-02`, not `LAB-02`). Serial logs use the same slug under `logs/device/labs/`.

```text
docs/labs/evidence/
└── <lab-slug>/                  # e.g. lab-02
    ├── REPORT-<LAB-ID>-<rev>.md
    ├── TEMPLATE-REPORT.md       # Optional per-lab copy (LAB-02 has one)
    └── runs/
        └── <run-id>/
            ├── run-manifest.json   # See schemas/run-manifest.v1.md
            ├── metrics.json        # See schemas/metrics.v1.md
            ├── notes.md
            └── video.md
```

**Run ID convention:** `YYYYMMDD-<phase>-<fixture>-<variant>-<seq>` (e.g. `20261011-phase-a-64-recycle-01`).

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
| `LAB-02` | [STATUS.md](lab-02/STATUS.md) (checklist; reports when published) |
| … | Add rows when reports land |

Update this table when publishing a report.
