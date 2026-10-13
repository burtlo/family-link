# LAB-02 workflow contract and remediation handoff

**Current implementation status:** W1–W7 independently verified; integrated host gate passed 146 tests and both configuration validators. W8 physical workflow validation and owner touch observation completed: manual PASS/UNMEASURED; scripted FAIL/UNMEASURED because inbox readiness prevented harness traversal. Host remediation is complete within O-07; application availability is deferred with evidence. No carousel repair or successful scripted traversal is claimed. [Execution and dispositions](evidence/lab-02/reviews/workflow-remediation/execution.md) · [application follow-up](evidence/lab-02/reviews/workflow-remediation/application-followup.md).

The planning-state statements below are preserved historical context; current outcomes and retry extensions are appended in the execution record.


**Status:** W0 workflow contract and plan independently verified; runtime fixes are not started. **Review base:** `main` HEAD `0b02b30086f1c8c8e985458912646b0d473b967b`. **Review:** [2026-10-12 record](evidence/lab-02/reviews/2026-10-12-workflow.md).

**Amendment status:** Prior revision verified; owner amendment O-09 independently verified by a fresh Luna verifier. W3 has not started; no runtime or private configuration files were changed in this amendment.

This document plans the LAB-02 Phase B host workflow before source fixes. It preserves the finding history, separates owner decisions from implementation choices, and does not claim carousel fixes or runtime behavior.

## Authority ledger

### Owner decisions and scope constraints

| ID | Owner-authorized intent |
|---|---|
| O-01 | The two canonical end-to-end commands are `make lab.lab02` (manual) and `make lab.lab02.scripted` (automated). |
| O-02 | Managed HTTP paging is the default for both commands; local fixtures are an explicit comparison mode. |
| O-03 | Manual traversal is free carousel traversal; loading cards may be traversed. |
| O-04 | A push preserves viewed message ID. Removing that viewed message selects the nearest remaining message. |
| O-05 | Host workflow manages server start, health, seed, scenario actions, capture, and cleanup. No separate manual server or curl actions. |
| O-06 | Keep deployment private and separate from shared lab experiment configuration. |
| O-07 | Define the workflow contract first; defer application functionality fixes to later work. |
| O-08 | Use Luna implementers and fresh, independent Luna verifiers sequentially, following the repository remediation approach. |
| O-09 | During W3, copy the existing `lab.phaseb_server_url` value once into the new separate lab/host configuration arrangement, then remove the obsolete field from private `local.yaml` and its example/schema/loaders/tests/docs. Preserve all unrelated private contents and credentials. Runtime supports only the new source: no fallback, compatibility, deprecation, migration window, or conflict-precedence behavior. |

### User-submitted issue and engineering choices

**User-submitted issue:** resolve exactly one connected registered kit and reuse the selected kit/port through build, flash, and capture. The bug report authorizes correction of wrong/ambiguous targeting; the exact resolver algorithm and representation are engineering choices, not additional owner decisions.

| ID | Engineering decision/proposal (no additional owner approval gate) |
|---|---|
| E-01 | Extend the existing tracked labs registry/schema for experiment settings; do not add another local config. This implements O-06. Generated build overlay is derived only. |
| E-02 | Preflight KIT/PORT consistency, select/freeze one registered target, and fail before flash on invalid or ambiguous identity. Exact API and algorithm are implementation choices. |
| E-03 | **Superseded by O-09.** The former legacy-URL compatibility proposal is retained by ID for history only; it is not implementation guidance. |
| E-04 | Derive/validate one reachable LAN client URL or use an explicit existing host config/environment override. Never use wildcard bind or guessed localhost; ambiguous interfaces fail. |
| E-05 | Proposed bounded host deadlines, reset/readiness procedure, nominal fixture/scenario, telemetry fields, and execution sequence are safety/verification choices, not product thresholds. |
| E-06 | Current physical-kit availability and the exact point of hardware scheduling are operational facts/planning choices. The requested workflow-first/application-deferred boundary is O-07. |
| E-07 | Keep `make lab.lab02.eager` as the existing comparison target; optional new comparison parameter names are implementation choices, not another canonical run command. |

Routine field names and exact destination for the copied URL can be selected during W3 and reviewed; destination remains a separate lab/host configuration arrangement, never the committed registry for private credentials. Owner decisions are O-01–09.

### Applicable sources

| Source at review | Authority and connection |
|---|---|
| `docs/AGENTS.md` | Repository boundaries and active-source authority. |
| `docs/standards/client-code-review-agent.md` | Normative finding, sequential fixer/independent-verifier, evidence, and append-only outcome process. |
| `docs/standards/client-application-coding-standards.md` | Normative operation ownership, bounded waits, terminal outcomes, identity, logs, L0–L3. |
| `docs/standards/labs-configuration.md`, `project-configuration.md`, `deployment-configuration.md` | Normative tracked lab registry/schema, host overrides, and private kit/secret boundaries; legacy URL currently in deployment config. |
| `docs/labs/lab-02-implementation-outline.md`, `assistant-guide.md` | Projected scenarios and current run instructions; planning/instructions, not approval of extra behavior. Assistant guide's separate server/curl flow must be updated. |
| `docs/labs/schemas/run-manifest.v1.md` | Required run provenance. Capture path currently writes `serial.meta.json`; complete manifest remains GAP-001 pending implementation verification. |
| `config/labs.registry.yaml`, `config/labs/labs.registry.schema.v1.json`, `config/deployment/example.yaml` | Current tracked host registry/schema and legacy `lab.phaseb_server_url` example. |

## Original findings and required verification

IDs and initial review wording are preserved in the [review record](evidence/lab-02/reviews/2026-10-12-workflow.md). Findings are open; no fix has been attempted.

| Finding | Trigger / effect | Requirement authority | Smallest fix; verification and evidence |
|---|---|---|---|
| `LAB02FLOW-F001`, confirmed P1 | Phase B make run requires a separately launched foreground server and manually invoked REST/curl actions; `lab_host.py` only builds/flashes/captures. | O-05; coding standard §§2–4, 10–11 (bounded operation, single owner, terminal outcome, evidence). | Managed server/health/seed/action/capture/cleanup; refuse foreign listener and stop owned process only. Verify normal path plus occupied port, health failure, timeout, cancel, cleanup and one-terminal-event boundaries. L2 scripted, then L3 workflow. |
| `LAB02FLOW-F002`, confirmed P1 | `deployment/example.yaml:34–38` and `lab_deployment_sdkconfig.py:29–41` place shared Phase B URL in private roster and inject it alongside Wi-Fi. | O-06 (separate private deployment) and O-09 (one-time copy/removal); labs/project/deployment configuration standards. | Add separate lab/host URL configuration and validation; during W3 copy the exact existing value once and remove only the obsolete field and its support. Preserve unrelated private settings/credentials. Test exact copy and preservation, rejection of old source at runtime/schema, validation of new source, URL derivation/override and ambiguity; inspect outputs for secrets. L1/L2. |
| `LAB02FLOW-F003`, confirmed P1 | Phase B build requires explicit KIT before device resolution. With `KIT=A PORT=deviceB`, `resolve_port` returns B while kit A's private build inputs can be used. | User-submitted wrong/ambiguous target issue; deployment configuration standard's registered serial binding/fail-before-write invariant. | E-02 validate consistency and freeze target through run. Verify no-KIT behavior, KIT-only unique selection, unknown/duplicate binding, KIT/PORT mismatch, disconnect, build-only explicit KIT without hardware, and run selection before flash. L1/L2; W8 observes target selection on hardware. |

The initial F003 wording also mentioned “late selection may choose a different target” as a possibility. **Clarification:** the confirmed mismatch path is `KIT=A PORT=deviceB`; the Phase B no-KIT path fails before device detection. Late selection alone is not asserted as a proven root cause. Preserve initial wording above as review history; this clarification narrows its causal claim.

**GAP-001 — evidence gap, not confirmed defect:** schema `run-manifest.v1.md` requires provenance beyond current `serial.meta.json`. W6 adds/validates a manifest and partial failure/interruption record. Parser success does not imply PASS.

**Scenario proof gap:** `S-page-snap` and `S-rm-focus` cannot be proven by current `set_focus(..., false)` harness and `snap_start focus=-1` trace; there is no focused message ID/action-receipt pairing. Add minimal scenario control/telemetry for proof. Timed manual traversal demonstrates traversal only, not an action during snap.

## Operator workflow contract

The canonical command owns one run. The mode changes traversal only. HTTP actions are host-managed and serialized; an optional scenario selector can request a named action set. Engineering proposal: default nominal scenario seeds fixture 64 with no mutation. Report the chosen fixture/scenario explicitly; scripted mode requires no operator touch. Manual briefing prints command, mode, fixture, kit identity (nickname only), expected free traversal, when capture is active, and remaining duration. Do not print URL credentials, Wi-Fi or tokens. Before any manual interaction/action, explicitly announce that capture is active.

1. Validate registry/schema, fixture, scenario, private credentials and reachable client URL from the new lab/host configuration. Resolve exactly one connected registered kit; reject zero/many, unknown or duplicate serial, inconsistent KIT/PORT, disconnection, occupied port, unsupported obsolete URL source, or ambiguous eligible LAN interface before flash.
2. Freeze kit, port, experiment settings, fixture/scenario, and build identity in a run context. Start the owned server, wait for application health, seed fixture and confirm seed receipt.
3. Build with resolved private per-kit Wi-Fi plus validated client URL. Flash while the flash tool exclusively owns the UART; serial capture must not open that port concurrently.
4. After flash tool releases UART, acquire the sole serial reader and log first. Apply a documented, explicitly controlled reset/reboot after capture is armed, then await a fresh boot/readiness marker from that capture. Record tool-initiated reset identity when available. Do not infer boot-ready from a marker that could have been missed or from a separate assumed readiness channel. If reset/readiness cannot be observed reliably, mark device evidence incomplete; do not claim boot confirmation.
5. Run manual free traversal or the scripted harness; execute managed server actions and correlate action receipt, message identity and snap identity. Keep one serial-reader owner and serialize state-changing actions.
6. Stop capture and only the server child owned by this invocation. Parse, write full manifest and failure evidence, then summarize `workflow_status` separately from the experiment/device verdict.

### State, deadlines, outcomes

| Stage | Completion / failure behavior |
|---|---|
| `PREFLIGHT` | Freeze valid configuration and kit. Any ambiguity fails before side effects. |
| `SERVER_START/HEALTH/SEED` | Bind owned process, prove app-level health, seed receipt. Occupied/foreign listener fails; never attach/kill it. Proposed startup, health, seed bounds: 10 s each. |
| `BUILD/FLASH` | Build and flash selected target. UART is exclusive to flasher; on failure clean up without scenario action. |
| `CAPTURE_ARM/BOOT_READY` | Sole serial reader owns port and log; controlled reset after capture starts; require fresh readiness marker within proposed 15 s. Missing marker yields incomplete device evidence. |
| `SCENARIO` | Capture active before interaction; each action has one receipt/outcome. Proposed action bound: 5 s. Cancel/timeout ends in FAIL or INCOMPLETE, never silent success. |
| `FINALIZE/CLEANUP` | Preserve partial artifacts; stop capture and owned server within proposed 5 s each. Ctrl-C follows same path. Kill only owned child, never by port/process name. |

All listed deadlines are proposed host safety bounds, not owner-approved performance/product thresholds. Tune from evidence. Exactly one terminal outcome per accepted host action/run. A summary has `workflow_status=PASS|FAIL|INCOMPLETE` and `experiment_verdict=PASS|FAIL|UNMEASURED|INCOMPLETE`. Workflow PASS means host stages, receipts, evidence writing and cleanup completed. Device/experiment verdict depends on scenario evidence and criteria set before run; a semantic failure can coexist with a technically completed host workflow. Build/parser/boot marker alone is not PASS.

### Scenario boundaries

| Scenario | Required evidence and interpretation |
|---|---|
| `S-fix-8/16/64/256`, `S-trav-20`, `S-rev`, `S-delay` | Record fixture, traversal and trace. Existing thresholds remain unapproved unless declared before run. |
| `S-push` | Correlate managed push receipt with before/after viewed ID; expect preservation per O-04. If firmware fails, workflow may still PASS while experiment verdict FAIL. |
| `S-rm-focus` | Remove viewed item during snap, pair receipt and snap, observe nearest remaining identity. Lack of app effect is experiment failure/open app finding, not a reason to call host transport broken. |
| `S-page-snap` | Pair page completion receipt with snap identity/timestamps. Current telemetry is insufficient. |
| Local fixture comparison / eager | Explicit comparison mode; preserve existing `make lab.lab02.eager` experiment target. No additional canonical end-to-end command. |

### Config ownership

| Setting | Authority / handling |
|---|---|
| Shared backend, fixture/scenario defaults, bind port, capture limits | Tracked labs registry/schema (exact field names are implementation choices). |
| Reachable firmware client URL | Derive and validate one eligible LAN address or accept explicit existing host config/environment override. Never pass wildcard bind or guessed localhost. |
| Kit serial binding and Wi-Fi | Private deployment `local.yaml`; redact from stdout/artifacts. |
| Build overlay | Generated derivative only, with no secrets in evidence. |
| Obsolete `lab.phaseb_server_url` | One-time W3 copy source only. Remove field and support after copying; runtime/schema do not accept it or define fallback/compatibility/conflict precedence. |

## Remediation units and dependency graph

All implementation remains future work. No app carousel semantics fix is in the host workflow units. A newly observed firmware semantic failure opens a separate application finding and does not by itself invalidate host workflow completion.

| Unit | Scope, prerequisites, and boundary | Meaningful acceptance / evidence |
|---|---|---|
| W0 — this document set | Document-only workflow contract. Only plan, review and one index link. | **VERIFIED** by fresh Luna audit `/root/workflow_audit_verify`; prior results and reviewed hashes preserved in the review record. Runtime findings remain open. |
| W1 — native test gate | Fix host semaphore stub/build test path only; no app behavior. Independent of W2/W3. Baseline native test command reports four passing tests but compile fails at `lab02_session.c:402`: undeclared `xSemaphoreCreateMutex` due missing host stub declaration. | Rerun exact command; production session compile covered. L0/L1. |
| W2 — kit selection | `scripts/lab_host.py`, device-selection adapter (`scripts/device_targets.py` or supported API), focused `scripts/tests/test_lab_host.py` tests. Independent of W1 and W3; chosen next for sequencing. | Unique connected registered target or valid explicit selection; reject unknown/duplicate binding and KIT/PORT mismatch before flash; prove build-only explicit KIT can run without hardware while run cannot. L1/L2. |
| W3 — config separation | `config/labs.registry.yaml`, `config/labs/labs.registry.schema.v1.json`, lab registry/config adapters, focused `scripts/tests/` config tests, deployment/labs config docs, and the private `config/deployment/local.yaml` only for the O-09 URL field copy/removal. Independent of W1/W2. Routine runs inspect private config read-only; during W3, the sole permitted private-file edit is copying this URL and removing its obsolete field, preserving all unrelated contents and credentials. Do not commit private credentials. | Verify one-time copy preserves the exact URL and unrelated private settings; old field is absent from example/schema/loaders/runtime support, and old-source-only runtime config is rejected or unsupported; new source validation, redaction, URL derivation/override and ambiguity pass. L1/L2. |
| W4 — managed server lifecycle | `scripts/lab_host.py`, proposed focused server lifecycle helper under `scripts/` if needed, `server/labs/lab-02-paging/server.py` health/seed interface, and focused `scripts/tests/` plus server tests. Depends on W3 server settings and W2 run target at integration. | Normal, foreign occupied port, health fail, timeout, cancellation, cleanup. No curl or foreign process control. L1/L2. |
| W5 — scenario coordinator/control | Host scenario client/coordinator in `scripts/lab_host.py` or a focused `scripts/lab02_workflow.py`; managed scenario actions in `server/labs/lab-02-paging/server.py`; LAB-02 harness/trace sources and focused host/server tests. Depends on W4 healthy API; uses W2/W3 context. No carousel fix. | Deterministic fixture/action receipts, serialization and trace pairing. Host action receipt/targeting is verified even if firmware fails to apply semantic update; classify effect failure separately. `S-rm-focus`/`S-page-snap` need identity telemetry. L1/L2. |
| W6 — evidence and summary | `scripts/lab_host.py` capture/finalize path, parser only if needed, `docs/labs/schemas/run-manifest.v1.md`, focused `scripts/tests/` writer/parser tests. Can begin after run-context and event interfaces (W2–W4); does not depend on all scenario correctness. | Full manifest plus partial failure/Ctrl-C/cleanup records, no secrets, separate workflow and experiment outcomes. L1/L2. |
| W7 — canonical wiring/docs | Root `Makefile`, `config/labs.registry.yaml` help/run defaults, `scripts/lab_host.py` entry point, `scripts/lab_registry.py` help, `docs/labs/assistant-guide.md`, LAB-02 docs, focused command tests. Integrate only after relevant W2–W6 host pieces independently pass. | Exactly two default commands, both managed HTTP; manual vs scripted cue; optional explicit fixture compare; help contains no manual server/curl. L1/L2. |
| W8 — hardware workflow validation | Requires integrated host workflow W2–W7 verified and physical registered kit; no USB kit currently connected. Does not require app semantic success to validate host. | L3 host/device workflow and owner touch observation. Report app push/removal behavior as separate experiment result; never claim carousel repair from workflow validation. |

**True prerequisites:** W4 needs W3's server/client config and W2's target context when integrated; W5 needs W4's managed healthy API and run context; W6 needs stable run-context/event interfaces, but can be implemented before W5 is complete; W7 needs relevant host components verified; W8 needs integrated host workflow verification and target access. W1, W2 and W3 have no dependency edges among them. The proposed execution order is W0, W1, W2, W3, W4, W5, W6, W7, W8 to keep edits sequential and expose the native test gate early. This is a chosen order, not a claimed dependency chain. No source edits, reviews, or verification overlap.

### Deferred application issue

Prior diagnostic artifacts include `logs/device/labs/lab-02/20261012-phaseb-capture-76ede5/serial.txt` (stack overflow in `lab02_sess` and semaphore assertions) and `logs/device/labs/lab-02/20261012-phaseb-capture-f8b961/serial.txt` (system event overflow). A blank viewport was also reported during earlier controller analysis. These observations do not establish root causes or current status; no application fix was in this stage. Preserve them for a future application review after workflow readiness. The native session host compile gap is separately recorded under W1. If W8 shows push/removal rendering or state preservation failure, append a distinct application finding with trigger/artifact, keep host workflow and experiment verdicts separate, and do not expand this host workflow assignment into carousel repair.

## Durable handoff and retry protocol

The orchestrator reads this plan, linked review, `docs/AGENTS.md`, full `docs/standards/client-code-review-agent.md`, and current source before resuming. Record current HEAD, diff, untracked paths and hashes; do not assume review HEAD is current. For each unit provide full finding text, source snapshot, trigger/effect, requirement/authority, smallest change, permitted paths, prerequisites, test boundaries and evidence level. Use one fresh `gpt-6-luna` fixer then a different fresh independent `gpt-6-luna` verifier, each scoped with `fork_turns: none`. Record exact diff/source hashes, commands/results, artifact provenance, verifier and per-finding disposition. Keep units sequential; append outcomes without rewriting initial findings.

On **not fixed** or regression, permit one focused repair followed by a new verifier. On evidence-only **unverified**, gather evidence without another code change. If retry still fails, append not fixed and block dependent units. Continue only independent work. W8 cannot claim device confirmation without L3 target evidence. Physical kit is needed only at W8; workflow authorization persists.

> **Resume prompt:** Continue LAB-02 from this plan and linked review. Check current state and final W0 audit; prior revision was independently VERIFIED, and O-09 is independently verified. W3 has not started. Start W1, or the first incomplete W1–W8 unit if later outcomes exist, with one fresh Luna fixer and a distinct fresh Luna verifier per unit. Revalidate affected evidence if intervening edits change the contract or implementation. Owner decisions are O-01–09; the wrong/ambiguous kit-target issue is recorded separately; remaining E items are engineering choices/proposals. Managed HTTP is default for the two canonical commands; local fixtures are explicit comparison. During W3 only, copy the existing `lab.phaseb_server_url` exactly into the selected separate lab/host config and remove that obsolete private field, preserving unrelated private contents/credentials; ordinary runs keep `local.yaml` read-only. Runtime has no old-source fallback or compatibility behavior. Do not alter carousel behavior, start manual servers/use curl, add private credentials to committed files, expose secrets, or commit. Keep host workflow outcome separate from firmware semantic result and append evidence/outcomes.

Final provenance addendum: current private deployment bytes differ from the saved post-W3 hash, with modification time before W8; attribution is pending. The workflow made no reported private edits after W3 and preserved the current file. See the [execution record](evidence/lab-02/reviews/workflow-remediation/execution.md) for hash-only evidence and the unchanged migration provenance limitation.

Owner clarified the later private hash change: they removed the top-level `lab` key after W3. This is an owner edit, separate from the workflow migration; see the [execution record](evidence/lab-02/reviews/workflow-remediation/execution.md).
