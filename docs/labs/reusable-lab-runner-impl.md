# Reusable device lab runner — implementation map

**Status:** Ready with explicit unknowns  
**Specification:** [Reusable device lab runner pattern](docs/labs/reusable-lab-runner.md)  
**Repository:** /Users/lynnfrank/src/family-link  
**Map scope:** Full specification  
**Evidence date:** 2026-10-17  
**Proposed output:** reusable-lab-runner-impl.md at the repository root.

## Summary

The proposal asks for one host run lifecycle and evidence format across registered labs, with experiment-specific behavior supplied by adapters. Server and scenario stages must be optional, so a no-server lab can use the same command and evidence envelope.

Today, the repository has much of the lifecycle in a LAB-02-specific runner. The general host path handles registry-based build, flash, capture, and parsing, but does not produce the same run manifest or event journal. LAB-02’s evidence writer fixes artifact paths to lab-02; the CLI, server settings, and scripted variant selection also contain LAB-02-specific routing. Server child output is discarded.

The main work is to define a versioned shared evidence contract, extract the runner behind an allowlisted adapter interface, migrate LAB-02 without changing its scenario proofs, and demonstrate the optional-server path. LAB-05 is a promising no-server proof candidate because the lab plan defines a transition/resource experiment and lists its server as none; it has no firmware project or host registration yet.

## Specification alignment

### Strengths in the specification

- It clearly identifies the reusable unit as the host run, leaving fixtures, protocols, parsing, and experiment verdicts with each lab.
- It describes a common lifecycle, evidence correlation, ownership, cleanup, and privacy constraints.
- Its migration sequence and acceptance criteria give implementers and verifiers observable outcomes.

### Strengths in the repository

- EvidenceRun already provides exclusive run directories, sanitized bounded events, a reserved terminal event, atomic manifests, and separate workflow and experiment verdicts ([scripts/lab02_evidence.py](scripts/lab02_evidence.py); [docs/labs/schemas/run-manifest.v1.md](docs/labs/schemas/run-manifest.v1.md)).
- The canonical LAB-02 runner already freezes inputs, revalidates the device before flashing, arms serial capture before reset, checks a fresh ready marker, and finalizes partial evidence ([scripts/lab02_canonical_run.py](scripts/lab02_canonical_run.py)).
- LAB-02 has substantial host tests for readiness, ownership, timeouts, cleanup, and evidence. The lab plan also describes a no-server LAB-05 experiment ([scripts/tests/test_lab02_canonical_run.py](scripts/tests/test_lab02_canonical_run.py), [scripts/tests/test_lab02_server.py](scripts/tests/test_lab02_server.py), [docs/labs/inbox-carousel-playback-evaluation.md](docs/labs/inbox-carousel-playback-evaluation.md#lab-05--transition-and-simultaneous-resource-peak)).

### Gaps and clarifications

| Requirement | Present state | Required state | Suggested resolution / question |
|---|---|---|---|
| Shared runner and adapters (R-architecture-001–002) | lab_host.py has generic host helpers but special-cases LAB-02 canonical runs; LabSpec has no adapter key. | A shared engine invokes explicitly allowlisted per-lab adapters. | Add an adapter key to the registry contract and resolve it through a fixed host-code allowlist. |
| Same lifecycle and evidence for every lab (R-goal-001, R-acceptance-001) | LAB-02 canonical runs produce manifests and events; generic capture writes serial metadata and optional metrics. | Manual and scripted runs for every registered lab produce the common shape. | Route run and scripted commands through the shared engine; leave .capture behavior explicit during migration. |
| Optional server stage (R-lifecycle-003, R-acceptance-005) | The registry workflow schema requires server, fixture, scenario, and deadline fields when workflow is present. lab_host.py imports LAB-02 modules at module load. | A no-server lab needs no fake settings or LAB-02 imports and records the server stage as not_configured. | Move workflow-specific behavior behind lazy, allowlisted adapters and define the common no-server stage representation. |
| Generic artifact roots and manifest version (R-artifact-002) | EvidenceRun and the v1 documentation allow only lab-02 artifact paths. | Paths derive from registered slugs and remain beneath approved roots; a generic manifest does not silently change v1 meaning. | Decide the new manifest document/version and preserve the existing v1 contract during migration. |
| Server diagnostics and privacy (R-artifact-001, R-acceptance-003) | The owned LAB-02 server has lifecycle events, but _spawn_owned sends stdout/stderr to DEVNULL. | Bounded server logs, serial, events, parser output, and cleanup outcomes are attached without exposing secrets or private endpoints. | Capture owned child output through a bounded, redacting writer and verify its exit status. |
| Build/run identity (R-identity-001) | LAB-02’s expected build ID includes run_id, kit, scenario, mode, backend, and client URL. | Build identity includes only firmware source, target, build options, and runtime configuration compiled into the image; run ID identifies the capture. | Separate the build fingerprint inputs from runtime context; list compiled configuration per adapter. |
| No-server integration proof (R-migration-005) | The LAB-05 plan says server: none, but only LAB-02 has a firmware project and host registration. | Prove the shared lifecycle with both server-backed and no-server labs. | Use a bounded LAB-05 proof run, subject to confirming how much of its experiment must be implemented for this runner milestone. |

## Requirement traceability

IDs below are map-only, derived in source-section order because the specification has no requirement IDs.

| Requirement | Source | Status | Present behavior and evidence | Delta / package |
|---|---|---|---|---|
| R-goal-001 — common lifecycle and evidence, including no-server labs | [Goal](docs/labs/reusable-lab-runner.md#goal) | partial | LAB-02 has a canonical workflow; the generic path does not. No no-server lab is registered. | P2, P5 |
| R-architecture-001 — separate shared run engine from lab workflow | [Proposed architecture](docs/labs/reusable-lab-runner.md#proposed-architecture) | partial | LAB-02 is routed to run_canonical; other labs use generic host functions in [scripts/lab_host.py](scripts/lab_host.py). | P2 |
| R-architecture-002 — adapter hooks and explicit allowlist | [Proposed architecture](docs/labs/reusable-lab-runner.md#proposed-architecture) | missing | [scripts/lab_registry.py](scripts/lab_registry.py) has no adapter field or driver allowlist; LAB-02 config is loaded separately. | P2 |
| R-lifecycle-001 — create unique run and evidence directory before preflight | [Lifecycle 1](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | EvidenceRun creates an exclusive directory before canonical preflight; generic runs do not use it. | P1, P2 |
| R-lifecycle-002 — freeze target, lab/build inputs, mode, and deadlines without sensitive values | [Lifecycle 2](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | Canonical LAB-02 freezes target and experiment inputs and filters sensitive manifest values. General registry-run context is not frozen into a common run record. | P1, P2 |
| R-lifecycle-003 — optional owned service with captured output and correlated health/seed receipts | [Lifecycle 3](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | ManagedLab02Server owns its child and checks run/fixture receipts; logs are discarded and server behavior is LAB-02-specific. | P2, P3 |
| R-lifecycle-004 — build/flash from frozen inputs and revalidate kit binding | [Lifecycle 4](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | Canonical LAB-02 revalidates before flash; generic build/flash exists, but is not governed by a shared run context. | P2 |
| R-lifecycle-005 — arm capture before reset and accept only fresh validated readiness | [Lifecycle 5](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | Canonical LAB-02 handles the reset edge and fresh marker. Generic capture records serial without this check. | P2, P4 |
| R-lifecycle-006 — manual/scripted activity and bounded, correlated actions | [Lifecycle 6](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | LAB-02 has manual/scripted behavior and scenario actions; scripted variant selection is tied to LAB-02 build variants in [Makefile](Makefile). | P2, P4 |
| R-lifecycle-007 — stop only owned resources; cleanup failure fails workflow | [Lifecycle 7](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | LAB-02 has owner-only server cleanup and bounded close behavior. Generic capture does not provide equivalent run cleanup/evidence. | P2, P3 |
| R-lifecycle-008 — parse partial evidence and keep verdicts independent | [Lifecycle 8 and verdict rule](docs/labs/reusable-lab-runner.md#standard-run-lifecycle) | partial | LAB-02 finalizes partial manifests and separates verdicts; generic runs do not share that contract. | P1, P2 |
| R-artifact-001 — correlate IDs and record common event fields and clocks | [Correlation and artifacts](docs/labs/reusable-lab-runner.md#correlation-and-artifacts) | partial | LAB-02 correlates run/action/device evidence, but there is no common event schema across registered labs. | P1, P2, P4 |
| R-artifact-002 — validate artifact roots and introduce generic manifest without changing v1 meaning | [Correlation and artifacts](docs/labs/reusable-lab-runner.md#correlation-and-artifacts) | partial | Writer validation is strong but fixed to LAB-02 paths and family-link.lab-run-manifest/v1. | P1 |
| R-artifact-003 — common metrics envelope with lab-specific metrics retained | [Correlation and artifacts](docs/labs/reusable-lab-runner.md#correlation-and-artifacts) | partial | [docs/labs/schemas/metrics.v1.md](docs/labs/schemas/metrics.v1.md) describes a LAB-02-oriented metrics document with some evidence fields. | P1, P4 |
| R-identity-001 — separate build fingerprint from run identity | [Build and run identity](docs/labs/reusable-lab-runner.md#build-and-run-identity) | partial | Current LAB-02 build ID includes per-run and device values, including run_id. | P1, P4 |
| R-migration-001 — extract parameterized evidence and lifecycle interfaces; retain v1 writer | [Migration 1](docs/labs/reusable-lab-runner.md#migration-plan) | partial | Safe writer primitives exist but are fixed to LAB-02 identity and roots. | P1 |
| R-migration-002 — route orchestration through LAB-02 adapter and remove CLI dispatch | [Migration 2](docs/labs/reusable-lab-runner.md#migration-plan) | missing | lab02_managed_canonical_plan and LAB-02 branches remain in lab_host.py; Makefile selects LAB-02 scripted variants. | P2, P4 |
| R-migration-003 — capture bounded, redacted owned service output and preserve exit status | [Migration 3](docs/labs/reusable-lab-runner.md#migration-plan) | missing | Child stdout/stderr are discarded. | P3 |
| R-migration-004 — run LAB-02 through shared engine while preserving proof and metrics | [Migration 4](docs/labs/reusable-lab-runner.md#migration-plan) | missing | Current proof exists in [scripts/lab02_scenarios.py](scripts/lab02_scenarios.py) and the LAB-02 parser, but migration has not occurred. | P4 |
| R-migration-005 — prove server-backed and no-server lab shapes | [Migration 5](docs/labs/reusable-lab-runner.md#migration-plan) | partial | LAB-02 covers server-backed behavior; no-server candidate LAB-05 is planned but not implemented. | P5 |
| R-migration-006 — update registry schema and author guide after flows work | [Migration 6](docs/labs/reusable-lab-runner.md#migration-plan) | missing | The registry schema and [docs/standards/labs-configuration.md](docs/standards/labs-configuration.md) describe the current registry/run workflow. | P6 |
| R-acceptance-001 — manual and scripted runs share manifest/event shape across labs | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | partial | LAB-02 canonical paths use EvidenceRun; generic and no-server paths are not covered. | P2, P4, P5 |
| R-acceptance-002 — correlate run IDs and report identity mismatches as incomplete evidence | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | partial | LAB-02 checks marker and server receipt identities. General correlation and mismatch classification are not shared. | P1, P2, P4 |
| R-acceptance-003 — attach logs/events/parser/cleanup outcomes without leaking secrets | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | partial | Serial redaction and structured events exist; server output is discarded and generic runs lack the common evidence envelope. | P1, P3 |
| R-acceptance-004 — owned resources have bounded cleanup; failures preserve partial evidence | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | partial | Canonical LAB-02 does this for its owned resources; generic runner coverage is absent. | P2, P3 |
| R-acceptance-005 — no-server labs need no fake settings or LAB-02 imports | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | missing | Only LAB-02 is registered; lab_host.py imports LAB-02 modules at module load. | P2, P5 |
| R-acceptance-006 — separate verdicts and explicit missing evidence | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | partial | LAB-02 writer/parser document these properties; generic labs have no common run/metrics envelope. | P1, P2 |
| R-acceptance-007 — preserve LAB-02 metrics and scenario proofs during extraction | [Acceptance criteria](docs/labs/reusable-lab-runner.md#acceptance-criteria-for-implementation) | partial | Existing parser and scenario verifier are present; their behavior after migration is unproven. | P4 |

## Repository map

| Path | Role and evidence |
|---|---|
| [Makefile](Makefile) | Pattern targets dispatch lab commands; scripted targets currently choose scripted or scripted-phaseb based on BACKEND. |
| [scripts/lab_host.py](scripts/lab_host.py) | Registry-driven build/flash/capture helpers plus LAB-02-specific canonical dispatch and unconditional LAB-02 imports. |
| [scripts/lab_registry.py](scripts/lab_registry.py) | Parses registry entries into LabSpec; currently no adapter key or generic run workflow interface. |
| [config/labs.registry.yaml](config/labs.registry.yaml) | Only registered lab is lab02; its workflow contains fixture server, scenario, and deadline settings. |
| [config/labs/labs.registry.schema.v1.json](config/labs/labs.registry.schema.v1.json) | The workflow_settings schema requires LAB-02 server and scenario fields. |
| [scripts/lab02_canonical_run.py](scripts/lab02_canonical_run.py) | Existing LAB-02 orchestration, build identity, serial capture, readiness validation, parsing, and cleanup. |
| [scripts/lab02_evidence.py](scripts/lab02_evidence.py) | LAB-02-specific evidence writer with bounded events and fixed artifact allowlists. |
| [scripts/lab02_server.py](scripts/lab02_server.py) and [scripts/lab02_scenarios.py](scripts/lab02_scenarios.py) | Owned fixture server lifecycle and LAB-02 scenario/action proof. The server process currently redirects output to DEVNULL. |
| [client/labs/lab-02-bounded-inbox/tools/parse_lab_trace.py](client/labs/lab-02-bounded-inbox/tools/parse_lab_trace.py) | LAB-02 parser; keep experiment metrics and reductions in this adapter/parser. |
| [docs/labs/schemas/run-manifest.v1.md](docs/labs/schemas/run-manifest.v1.md) and [docs/labs/schemas/metrics.v1.md](docs/labs/schemas/metrics.v1.md) | Current evidence and metrics documentation; manifest roots and metric requirements are LAB-02-oriented. |
| [docs/labs/inbox-carousel-playback-evaluation.md](docs/labs/inbox-carousel-playback-evaluation.md#lab-05--transition-and-simultaneous-resource-peak) | Defines the no-server LAB-05 resource/transition experiment proposed as a proof candidate. |
| [scripts/tests/](scripts/tests/) | Existing tests cover LAB-02 runner, evidence, server, scenario, registry, and host paths; none establish a reusable no-server adapter path. |

## Workstreams and decisions

### Common evidence contract

The new writer needs configurable lab identity, evidence slug, project path, and approved artifact roots while retaining current v1 behavior. The generic manifest and metrics envelope need stable version identifiers and fields before implementers lock schemas.

### Runner and adapter boundary

The shared engine should own targeting, run context, stage deadlines, capture/reset ordering, cleanup, redaction, and finalization. Adapters should own fixture/server protocol, readiness and harness markers, trace interpretation, parsing, and experiment verdicts. The Makefile’s scripted targets and module imports must stop assuming LAB-02-specific variants or modules.

### LAB-02 migration and service logs

Keep lab02_scenarios.py and the LAB-02 parser as adapter behavior. Add bounded stdout/stderr capture for the owned fixture server, retaining exit status and redacting secrets and private endpoints.

### No-server proof

LAB-05 is the closest existing match: its plan defines transition and simultaneous-resource capture and lists no server. Its code does not exist, so the proof package should agree on a bounded implementation scope before adding it to the host registry.

### Clarifications to resolve during implementation

- Choose the new generic manifest document/version and define how v1 output remains available during migration.
- Decide whether the common metrics envelope extends family-link.lab-metrics/v1 or receives a new document/version, and define its required fields and output location.
- Define whether a run or fixture identity mismatch sets workflow status to INCOMPLETE or FAIL, while ensuring the evidence itself is reported incomplete.
- Confirm LAB-05 as the no-server proof and bound how much of its experiment must be implemented for the runner acceptance.

These choices are visible and do not change the proposed package boundaries, so the specification is Ready with explicit unknowns.

## Implementation packages

| ID | Outcome | Requirements | Depends on | Main touchpoints | Verification |
|---|---|---|---|---|---|
| P1 | Versioned shared evidence and metrics contracts; parameterized safe writer | R-lifecycle-001–002, R-lifecycle-008, R-artifact-001–003, R-identity-001, R-migration-001, R-acceptance-006 | — | scripts/lab02_evidence.py or extracted common module; evidence schemas/docs; evidence tests | Writer tests for generic slugs, path containment, version compatibility, missing evidence, and independent verdicts |
| P2 | Shared run engine and allowlisted adapter dispatch for manual/scripted mode | R-goal-001, R-architecture-001–002, R-lifecycle-001–008, R-migration-002, R-acceptance-001–002, R-acceptance-004–006 | P1 | scripts/lab_host.py, scripts/lab_registry.py, config/labs.registry.yaml, registry schema, Makefile | Adapter dispatch tests for server and no-server shapes; mode-independent common artifacts; registry validation |
| P3 | Bounded, redacted logs and exit status for owned fixture services | R-lifecycle-003, R-lifecycle-007, R-artifact-001, R-migration-003, R-acceptance-003–004 | P2 | scripts/lab02_server.py, shared log/redaction support, server tests | Child output bounds/redaction, exit status, deadline, and owner-only cleanup probes |
| P4 | LAB-02 runs through the shared engine with established proofs preserved | R-lifecycle-003–008, R-artifact-001, R-identity-001, R-migration-004, R-acceptance-002–004, R-acceptance-007 | P1–P3 | scripts/lab02_canonical_run.py, scripts/lab02_scenarios.py, LAB-02 parser and tests | Compare manual/scripted manifest, events, serial, and metrics; preserve scenario proof and missing-evidence behavior |
| P5 | No-server lab proves optional stages end to end | R-goal-001, R-migration-005, R-acceptance-001, R-acceptance-005 | P2, P4 | Candidate LAB-05 project, registry entry, parser/report as needed | Run manual and scripted modes; prove not_configured server stage, shared envelope, and no LAB-02 imports/settings |
| P6 | Registry and author documentation describe the working shared flow | R-migration-006 | P4, P5 | Registry schema/docs, docs/standards/labs-configuration.md, docs/labs/assistant-guide.md, lab index and evidence schema docs as appropriate | Registry validation and docs-to-CLI behavior review after both proof flows pass |

### Implementer briefs

#### P1 — Shared evidence contracts

- **Goal:** Define stable generic manifest and metrics envelopes and make the writer accept registered lab identity and validated artifact roots.
- **Deliverables:** Version identifiers and field definitions; parameterized writer; v1 compatibility path; tests for sanitization, bounds, containment, partial runs, and verdict separation.
- **Out of scope:** Changing LAB-02 metrics names or scenario reductions.
- **Touchpoints:** Evidence writer, run manifest and metrics documentation, evidence tests.
- **Dependencies and assumptions:** Resolve the manifest and metrics version questions before freezing schemas. Derive paths from trusted registry values and validate resolved paths beneath approved roots.

#### P2 — Shared runner and adapter dispatch

- **Goal:** Make the host run lifecycle work without lab-specific branches in common CLI logic.
- **Deliverables:** Common run context and stage/event model; adapter protocol and explicit allowlist; mode independent of build variant; optional service/scenario stages; no-server status; remove LAB-02 imports from the generic path.
- **Out of scope:** Moving experiment semantics or metric reductions into the engine.
- **Touchpoints:** Host CLI, registry model/schema, Makefile scripted targets.
- **Dependencies and assumptions:** P1 contract; adapters provide only registered hooks, not arbitrary code or unconstrained YAML behavior.

#### P3 — Owned service logs

- **Goal:** Preserve useful diagnostics from an owned service without unbounded storage or secret leakage.
- **Deliverables:** Bounded stdout/stderr capture attached to the run, redaction, observed exit status, and tests for cleanup and output limits.
- **Out of scope:** Changing LAB-02 HTTP protocol or fixture behavior.
- **Touchpoints:** ManagedLab02Server._spawn_owned, shared log sink, server tests.
- **Dependencies and assumptions:** P2 supplies the run artifact location and cleanup lifecycle. Redaction must handle credentials split across read chunks.

#### P4 — LAB-02 adapter migration

- **Goal:** Run existing LAB-02 manual and scripted workflows through the common engine.
- **Deliverables:** LAB-02 adapter for server setup, readiness/harness markers, scenario actions, parser, and experiment classification; preserved serial and proof behavior.
- **Out of scope:** Changing LAB-02 acceptance criteria, scenarios, or metric reductions.
- **Touchpoints:** Canonical runner, scenario coordinator, parser, relevant host/evidence/server tests.
- **Dependencies and assumptions:** Use the new build fingerprint rules; run and kit identity should not trigger a rebuild unless compiled inputs change.

#### P5 — No-server integration proof

- **Goal:** Demonstrate that a registered no-server lab completes manual and scripted runs with the same common artifacts.
- **Deliverables:** A bounded proof lab, proposed as LAB-05, its registration and any minimal project/parser needed for both modes.
- **Out of scope:** Completing the full resource research program unless required by the agreed proof scope.
- **Touchpoints:** LAB-05 plan and project, registry, shared runner.
- **Dependencies and assumptions:** Confirm the minimum experiment behavior needed. The existing LAB-05 plan gives a resource/transition scenario and identifies no server, but no implementation exists.

#### P6 — Documentation and registry guidance

- **Goal:** Make the published registry and author workflow match the proven engine.
- **Deliverables:** Updated schema and validation guidance; revised host command and adapter instructions; common evidence links.
- **Out of scope:** Rewriting unrelated lab plans.
- **Touchpoints:** Registry schema and standards, assistant guide, lab index and evidence schema docs as appropriate.
- **Dependencies and assumptions:** Follow the spec’s ordering: update registry schema and author guide after server-backed and no-server flows work.

### Verifier briefs

#### P1 — Verification

- **Must prove:** Requirements R-artifact-002–003 and R-acceptance-006: v1 meaning remains stable; generic paths stay under approved roots; missing evidence and verdicts remain truthful.
- **Tests / probes:** Extend scripts/tests/test_lab02_evidence.py with non-LAB-02 paths, malicious/traversal paths, generic schema examples, and failure finalization. Run make test.
- **Regression watch:** Event bounds, reserved terminal row, atomic manifest retry, and current LAB-02 artifact references.
- **Limits:** No hardware required for writer tests. Tests were not run for this map.

#### P2 — Verification

- **Must prove:** Requirements R-architecture-001–002 and R-acceptance-001, R-acceptance-005: allowlisted dispatch, common manual/scripted envelopes, no-server operation without fake workflow settings or LAB-02 imports.
- **Tests / probes:** Add mock-adapter dispatch tests for service-backed and no-server labs; inspect Make target arguments and import paths; validate registry with the documented python scripts/lab_registry.py validate; run make test.
- **Regression watch:** Kit binding revalidation, scripted LAB-02 build selection, make lab.help, and direct build/flash/capture commands.
- **Limits:** Mock tests can verify dispatch; device runs are needed later to prove physical reset and readiness.

#### P3 — Verification

- **Must prove:** Requirements R-artifact-001 and R-acceptance-003–004: owned server output is bounded, redacted, attached, and its cleanup/exit outcomes are retained.
- **Tests / probes:** Extend scripts/tests/test_lab02_server.py with fake child output containing secrets, split-secret chunks, high-volume output, early exit, timeout, and cleanup failure; run make test.
- **Regression watch:** Never terminate a foreign listener; retain startup, health, seed, action, and cleanup deadlines.
- **Limits:** Unit tests can exercise child-process handling; final server run should also inspect a real captured log.

#### P4 — Verification

- **Must prove:** Requirements R-migration-004 and R-acceptance-002, R-acceptance-007: LAB-02 artifacts match the common contract and scenario proof/parser behavior remains intact.
- **Tests / probes:** Run focused canonical, scenario, parser, evidence, and server suites, followed by make test; execute manual and scripted LAB-02 runs and compare manifests, event journals, serial, metrics, and scenario receipts.
- **Regression watch:** Fresh post-reset marker only; fixture/build identity mismatch; action correlation; missing trace evidence; cleanup failure; unchanged LAB-02-specific metrics/proofs.
- **Limits:** Hardware proof needs a registered BOX-3, serial access, ESP-IDF, fixture server network settings, and an available kit. This map did not run those flows.

#### P5 — Verification

- **Must prove:** Requirements R-goal-001 and R-acceptance-005: server stage is not_configured, no fake server configuration is required, and the no-server lab uses the same manual/scripted manifest and event shape.
- **Tests / probes:** Add runner-level tests asserting no LAB-02 imports and no server configuration access; run both modes on the proof lab and inspect partial failure artifacts too.
- **Regression watch:** Generic target selection, build/run identity separation, readiness semantics, and parser output under the common metrics envelope.
- **Limits:** A device-level run requires the target hardware and project/toolchain. LAB-05 has a plan but no current code or registration.

#### P6 — Verification

- **Must prove:** Requirement R-migration-006: docs and registry guidance describe actual commands, fields, and adapter responsibilities.
- **Tests / probes:** Run python scripts/lab_registry.py validate, make config.labs, make lab.help, and make test; review command examples against both integration proof labs.
- **Regression watch:** Registry schema compatibility, stale LAB-02-only instructions, and unsupported claims about no-server support.
- **Limits:** Validation commands do not substitute for the hardware integration evidence from P4 and P5.

## Suggested implementation order

**Package sequence:** P1 → P2 → P3 → P4 → P5 → P6

### Order rationale

P1 establishes the schema and safety rules the engine will write. P2 builds the shared lifecycle against that contract. P3 adds owned service logging once the engine provides run-scoped artifacts. P4 migrates the server-backed LAB-02 flow and protects its scenario behavior. P5 then proves optional server stages against a no-server shape. P6 documents only flows that have been exercised.

## Validation resources and limits

| Resource | Use or limitation |
|---|---|
| make test | Repository’s documented Python test discovery command; not run during this planning pass. |
| python scripts/lab_registry.py validate | Existing registry structure and semantic validation command. |
| make config.labs, make lab.help | Inspect registry snapshot and operator-facing lab list. |
| scripts/tests/test_lab02_* | Existing LAB-02 host, evidence, server, scenario, config, and parser regression hooks. No reusable adapter or no-server runner coverage exists yet. |
| LAB-02 hardware workflow | Needs a registered kit, serial access, ESP-IDF, managed fixture server/network configuration, and hardware availability. Not exercised. |
| LAB-05 proof | Plan exists, but firmware project, registry entry, and runner integration do not. Scope must be bounded before implementation. |

## Cross-cutting risks

| Risk | Impact | Mitigation or decision needed |
|---|---|---|
| Manifest compatibility | Reinterpreting v1 could invalidate existing evidence consumers. | Add a new document/version and preserve v1 behavior during migration. |
| Secrets in logs or manifests | Captured server diagnostics may contain credentials or private endpoints. | Redact streaming output, cap bytes, and test secrets split across chunks. Keep private values out of manifest fields. |
| Resource ownership and interruption | A failed run could leave a child process, serial reader, or incomplete evidence. | Keep ownership explicit, bound every wait/cleanup, and finalize partial evidence in all exit paths. |
| Build fingerprint inputs | Including run-only values forces needless per-run builds; omitting compiled configuration can misidentify an image. | Define build inputs per adapter and test that changing run_id alone does not change build identity. |
| Hardware proof coverage | Host mocks cannot prove reset-edge freshness or real device readiness. | Keep unit tests and hardware verification separate; report unavailable hardware as a validation limit. |

## Related materials

- [LAB-02 implementation outline](docs/labs/lab-02-implementation-outline.md)
- [Labs configuration standard](docs/standards/labs-configuration.md)
- [LAB repository layout](docs/labs/repository-layout.md)
- [LAB-02 run manifest v1](docs/labs/schemas/run-manifest.v1.md)
- [LAB metrics v1](docs/labs/schemas/metrics.v1.md)
- [LAB-05 transition/resource experiment](docs/labs/inbox-carousel-playback-evaluation.md#lab-05--transition-and-simultaneous-resource-peak)

