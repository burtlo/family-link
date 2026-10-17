# Reusable device lab runner pattern

Status: **architecture proposal based on the LAB-02 review**. The existing LAB-02 workflow remains authoritative for running LAB-02 until this proposal is implemented.

## Goal

Give each hardware lab the same run lifecycle and troubleshooting record while leaving experiment behavior, fixtures, server protocol, trace parsing, and success criteria with that lab. A lab with no server should still use the same run command and evidence envelope, with server stages marked `not_configured`.

The reusable unit is the **host run**, not the lab firmware. Device modules remain experiment-specific until independently reviewed for promotion into `client/shared/`.

## LAB-02 review

### Reusable today

- `scripts/lab_host.py` already resolves registered labs and variants and provides generic build, flash, serial capture, help, and parser dispatch.
- The canonical LAB-02 workflow demonstrates a useful run sequence: freeze the target and experiment inputs, own and verify the fixture server, build and flash, arm serial capture before reset, require a fresh boot marker, run manual or scripted activity, stop owned resources, parse, and finalize evidence.
- `EvidenceRun` provides strong evidence behavior: unique run directories, sanitized append-only events, bounded event counts and row sizes, a reserved terminal event, atomic manifest writes, explicit unknown values, and separate `workflow_status` and `experiment_verdict`.
- LAB-02 correlates host-side server activity with device trace using `run_id`, `action_id`, snap identity, and receipt identity. Its secret-aware serial reader and build/fixture identity checks are useful runtime safeguards.
- The trace parser retains raw counts and missing-evidence state instead of translating absent measurements into zero or a pass.

### Tied to LAB-02 today

- `lab_host.py` contains LAB-02-only backend selection, scripted variant mapping, and a managed canonical-run branch. The generic registry build/flash/capture path does not provide the same run guarantees.
- `lab02_canonical_run.py` names LAB-02's ready marker and harness format, Box-3 board, project and build paths, fixture identity, server URL, scenarios, and metric parser assumptions directly.
- `lab02_evidence.py` fixes `lab_id` to `LAB-02` and fixes both the evidence and serial-log allowlisted paths to `lab-02`. The v1 manifest documentation has the same path assumption.
- `lab02_workflow_config.py` validates LAB-02 fixture sizes, scenarios, network discovery, and server configuration. The registry `workflow` schema also encodes those LAB-02 fields and scenario names.
- `lab02_server.py` owns LAB-02 health, seed, and scenario-action HTTP semantics. `lab02_scenarios.py` checks inbox-specific snap, page, and action proof.
- The basic generic `capture_lab` path writes a serial metadata file and optional metrics, but it does not create the canonical event journal and terminal run manifest or verify a fresh post-reset ready marker.

### Troubleshooting gap

The host captures server lifecycle and action outcomes as events, but `ManagedLab02Server._spawn_owned` redirects the fixture server's stdout and stderr to `DEVNULL`. A server exception or request-level diagnostic can therefore be absent from the run artifacts. A shared runner should preserve a bounded, redacted server log alongside the structured lifecycle events.

## Proposed architecture

Separate a shared **run engine** from a per-lab **workflow adapter**:

```text
make lab.<name> [mode=manual|scripted]
            │
            ▼
       lab_host CLI
            │
            ├── shared run engine
            │     target freeze · run ID · stages · capture · cleanup · evidence
            │
            └── registered lab adapter
                  build inputs · fixture/server protocol · ready marker · scenarios
                  parser · experiment-specific verdict
```

The shared engine owns hardware targeting, run directories, stage deadlines, build/flash callbacks, serial-reader ownership, capture-before-reset ordering, fresh-boot verification, manual/scripted mode handling, bounded cleanup, log redaction, event journaling, and manifest finalization. It should not know what a carousel snap, login stage, or audio playback means.

The adapter provides only experiment-specific behavior:

| Adapter responsibility | Examples |
|---|---|
| Run inputs and build identity | Fixture revision, scenario, relevant build options, server address needed by firmware |
| Fixture service | Optional server command, health and seed checks, fixture hash, scenario actions |
| Device markers | Ready-marker parser and identity checks; optional scripted-completion marker |
| Serial events | Parse or classify lab-specific trace lines; correlate experiment actions with device events |
| Results | Metrics parser and optional experiment-specific verdict; absent measurements stay unknown |

Prefer a small adapter interface backed by a controlled driver registry over making the shared runner interpret arbitrary lab-specific YAML. The host registry should continue to hold operator-facing names, project paths, variants, run defaults, and an adapter key. Adapter keys should resolve through an explicit allowlist in host code.

## Standard run lifecycle

Every `make lab.<name>` and `make lab.<name>.scripted` run should follow the same stages. Server and scenario stages are optional, but their presence or absence is recorded.

1. **Create** a new, non-reusable `run_id`; create the evidence directory before preflight so failed attempts still have a record.
2. **Freeze** the selected kit and USB identity, lab revision, firmware source/build inputs, mode, scenario, fixture, server config, and deadlines. Do not put USB serials, credentials, private URLs, or raw `sdkconfig` in the manifest.
3. **Prepare** the optional fixture server. Start only the child owned by this run; capture its output; verify health and seed receipts carry the same `run_id` and expected fixture identity.
4. **Build and flash** from the frozen inputs. Record source, binary, sdkconfig, and toolchain provenance when observed. Revalidate the registered kit binding before flashing.
5. **Arm capture, then reset.** Preserve pre-reset serial bytes for diagnosis, but only accept a ready marker read after the reset edge. Validate the marker against the expected firmware/run inputs.
6. **Run activity.** Manual mode prints the operator steps. Scripted mode waits for a validated harness terminal. Scenario controls must use bounded deadlines and correlated action receipts.
7. **Stop capture and clean up** only resources owned by the run, including the server. Cleanup failure makes the host workflow fail even if the device appeared to complete.
8. **Parse and finalize** whatever evidence exists, including partial evidence on interruption or failure. Parsing success does not imply workflow success or experiment success.

The workflow verdict and experiment verdict remain separate. For example, a completed capture can have an experiment result of `FAIL`, `INCOMPLETE`, or `UNMEASURED`; a parser exit code cannot promote either verdict to `PASS`.

## Correlation and artifacts

Carry the same `run_id` through the host manifest, event journal, serial ready/trace markers where supported, server health/seed/action requests, fixture-server logs, and metrics. Give server actions a unique `action_id`; retain device operation/session/snap identifiers when available. Events should include a source, event kind, monotonic timestamp, and observed outcome. Keep wall-clock timestamps in the manifest for cross-run indexing.

Suggested layout:

```text
docs/labs/evidence/<evidence_slug>/runs/<run-id>/
├── run-manifest.json       # Frozen context, stage status, verdicts, artifact references
├── events.jsonl            # Bounded host/server/device event journal
├── server.log              # Optional bounded stdout/stderr from the owned service
├── host.log                # Optional host diagnostics
└── metrics.json            # Lab parser output; common envelope plus lab-specific data

logs/device/labs/<evidence_slug>/<run-id>/
├── serial.txt              # Redacted capture, including pre-reset context
└── serial.meta.json        # Capture settings and operator instructions
```

The event writer should keep LAB-02's current safety properties. Artifact paths must be derived from the registered evidence slug and validated beneath approved roots; a path from an adapter must never escape those roots. A versioned generic manifest should replace the current LAB-02-only path allowlist without silently changing the meaning of `family-link.lab-run-manifest/v1`.

Use a common metrics envelope for run ID, parser/schema version, evidence completeness, trace-loss state, and missing measurements. Keep metric names and experiment-specific reductions inside each lab parser or a namespaced lab-specific section. A future common trace convention can standardize event framing and identity fields without requiring unrelated experiments to share event vocabularies.

## Build and run identity

Keep **build identity** and **run identity** distinct. The build fingerprint should cover firmware source, target, build options, and only runtime configuration that is compiled into the image. The `run_id` identifies one capture and should be carried as runtime context where possible. LAB-02 currently includes run ID, kit, and scenario in its expected firmware build ID; that is useful for proving one run's image but would force unnecessary per-run builds if generalized unchanged.

## Migration plan

1. **Extract the common evidence and lifecycle interfaces.** Parameterize `EvidenceRun` with lab ID, evidence slug, project path, and artifact roots; introduce common run context, stage/event types, and mode independent of build variant. Keep the existing LAB-02 v1 writer behavior available during migration.
2. **Move orchestration behind a lab adapter.** Have the shared engine call LAB-02 adapter hooks for fixture configuration, ready-marker validation, scenario control, parsing, and experiment classification. Remove `lab02_managed_canonical_plan` and LAB-02-specific path/variant dispatch from the common CLI as those hooks take ownership.
3. **Capture owned service output.** Stream child stdout/stderr into a bounded per-run log with redaction, preserve exit status, and retain structured health/seed/action events. Keep the current owner-only stop behavior and deadlines.
4. **Run LAB-02 through the shared engine** and compare its event, manifest, serial, and metrics artifacts with the established contract. Preserve the current LAB-02 scenario verifier and parser as adapter code.
5. **Prove the abstraction with two shapes of lab:** one lab with a managed fixture server and scripted actions, and one without a server. LAB-02 covers the first; a small no-server lab (for example a bounded UI/resource experiment) should validate optional stages before more labs depend on the framework.
6. **Update the registry schema and author guide** only after those flows work. A new lab should then need its firmware/server tree, an adapter only when behavior is lab-specific, a registry entry, and its parser/report—not a new copy of run orchestration.

## Acceptance criteria for implementation

- A manual and scripted run produce the same common manifest/event shape for every registered lab.
- `run_id` correlation works across host, serial, optional server, actions, and metrics; mismatched run or fixture identity is reported as incomplete evidence.
- Server stdout/stderr, serial, stage events, parser output, and cleanup outcomes are attached to the run; secrets and private endpoints are not exposed.
- Every started resource has an owner and bounded cleanup path; failure preserves a partial manifest and any logs already collected.
- No-server labs do not need fake server settings or LAB-02 imports.
- Workflow and experiment verdicts remain independent, and missing evidence remains explicitly missing.
- Existing LAB-02 scenario-specific metrics and proof checks stay unchanged while the runner is extracted.
