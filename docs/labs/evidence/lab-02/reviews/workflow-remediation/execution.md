# LAB-02 remediation execution

Owner request: implement `docs/labs/lab-02-workflow-remediation-plan.md` as orchestrator. The plan and [original review](../2026-10-12-workflow.md) supply full findings, authority O-01–09, permitted paths, dependencies and retry boundaries.

Initial state: branch `main`, HEAD `0b02b30086f1c8c8e985458912646b0d473b967b`. Existing WIP: modified `docs/labs/README.md`; untracked remediation plan and original review directory. No runtime changes at entry. Initial plan SHA-256 `40647abf5f1e8aef6277a6a6a048c23cb4d58ac74e807926c04d3e95ca0b05c7`; original review SHA-256 `938d9c0ac371ef4d04c0ca254ca777a653517801ca86863a4a568e8dc5b740e5`. Initial tracked diff saved at `/tmp/lab02-remediation-initial.diff` (local transient provenance).

Execution order W1–W8 is sequential with one fresh Luna fixer and a different fresh read-only Luna verifier for each unit. No commits, archive edits or carousel repairs. W1/W2/W3 independent; W4 depends on W2/W3 integration, W5 on W4, W6 on stable run context/events, W7 on verified components, W8 on integration and physical hardware. Evidence never substitutes host checks for target confirmation.

## Outcomes (append-only)

Implementation underway. No runtime finding disposition yet.

### W1 — verified fixed (L0/L1)

Fixer `/root/w1_fix`; independent verifier `/root/w1_verify` (both fresh Luna). Two host stub files add the missing `xSemaphoreCreateMutex` declaration and initially available binary mutex implementation. Production session unchanged. Clean HEAD archive reproduces original failure; exact native/architecture command passes 5 tests after fix. Verifier's bounded mutex harness checks initial token, exclusion, blocking/wake and reuse. [Verification artifacts](w1-verification/verification-report.txt); [exact diff](w1.diff). Source SHA-256: semphr.h `bcc9f6c12dd8991c74ebe1c87f11cda95ca08ea3c97e1fb8f28f65bddb71d406`; session_host_stubs.c `3e92c59f67fbcb915c3c67c45981656b62ecc4806ef086e50c4aeaeb72c870e6`. No L2/L3 claim. W2 begins next; runtime workflow findings remain open.

### W2 initial verification — not fixed (scoped regression)

Fixer `/root/w2_fix`; independent verifier `/root/w2_verify`, fresh Luna. Core F003 CLI paths independently pass L2 simulated host adapter: target selection before build, no-KIT unique selection, KIT/PORT mismatch before writes, and build-only explicit nickname without USB. Twelve host tests pass. A new helper regression remains: direct `capture_lab(kit="desk")` resolves nickname to kit A but compares the raw nickname literally against A's id and rejects it. Only current repo caller main normalizes first, but supported helper behavior must be preserved. [Report](w2-verification/report.txt); [initial diff](w2-initial.diff). Report SHA-256 `f0f285f7cd7a29d1bbe580cebfbad95ccfbf55279ad736ffe19c5a0fc6cb83e4`. One focused repair/new independent verifier follows; W2 not yet accepted. No L3 claim.

### W2 focused repair — verified fixed (L1/L2 simulated host)

Fixer `/root/w2_repair`; fresh independent verifier `/root/w2_reverify`. Capture resolves nicknames against the frozen deployment before checking canonical kit identity. Fourteen tests pass. The independent actual-main simulated harness confirms no-KIT select/reuse, matching alias/port, mismatch/zero/many before side effects, direct flash rejection, build-only without USB, and capture aliases with internally resolved/supplied target plus mismatched KIT before flash. [Report](w2-reverification/report.txt); [repair diff](w2-repair.diff). Final SHA-256: lab_host.py `d5495a9e695bb06c1dd8cbc6ab0fa2b867b075b3e568eec2742fbf2461a45499`; test_lab_host.py `a5321e0e33dc5435b789a73e8c3188ab007f7ddff93f64229c1e0b2f578a4c91`. LAB02FLOW-F003 host correction verified; physical target confirmation remains W8 (L3), without waiver. Initial not-fixed history retained.

### W3 initial verification — not fixed (loader validation gap)

Fixer `/root/w3_fix`; independent verifier `/root/w3_verify`. Separate host URL, old-field runtime removal, malformed/credential/loopback/wildcard URL redaction and rejection, ambiguous-interface rejection, URL build digest invalidation and validators pass. Direct workflow loader bypasses registry JSON Schema and accepted out-of-range port and unsupported fixture/scenario. Normal CLI bypass was not established; fix the public loader boundary. [Verification](w3-verification/verification-report.json). One focused repair follows.

Private migration incident: first copy/removal succeeded, but the fixer accidentally replaced the copied host value with an example. The exact original was recovered from two existing generated build artifacts; reconstruction matched the saved first-migration host hash `feb1b42e2dbe6a10c1691e21df5cca633ae1f8207f58d644f26ba44bcbba54ae`. Candidate also matched the original build input digest using preserved private profile. Saved post-migration private hash remains `c3edfdcb494e99500dbbd2433fe7e02181afe981be25bae2e5fd03fe3b08c215`. No unrelated private edit or credential output is claimed. First verifier confirmed current hashes/obsolete-field absence but had no pre-migration copy for an independent byte comparison. Exact migration algorithm and saved copy/recovery proofs are available under `/tmp/lab02-w3-fix`; further evidence collection must not edit private config again. Migration error history retained.

### W3 focused repair — verified fixed (L1/L2 host; migration provenance limit)

Fixer `/root/w3_repair`; fresh independent verifier `/root/w3_reverify`. Configured registry schema enforced at public loader boundary; supported fixture/scenario catalogs enforced. Original direct invalid values now reject with redacted errors. Twenty-five fresh workflow/overlay/deployment tests and both validators pass. LAB02FLOW-F002 host correction verified. [Final report](w3-reverification/report.txt) and [source hashes](w3-reverification/source-hashes.txt).

Migration provenance limitation: original private pre-migration bytes were not archived, so no independent actual-private before/after comparison is claimed. Exact copy is corroborated by saved first migrated-host hash reconstruction and agreeing generated artifacts/digest; unchanged post-removal private hash, recorded substring-removal assertion and an independent synthetic fixture support unrelated-content preservation. No further private edits are permitted by this workflow. W4 proceeds on verified configuration behavior; no hardware/L3 claim.

### W4 initial verification — not fixed (lifecycle boundaries)

Fixer `/root/w4_fix`; independent verifier `/root/w4_verify`. Eight lifecycle/server tests and syntax checks pass. Normal real owned child/health identity/seed fixture receipt, foreign occupied listener exclusion and normal cleanup verified. Remaining defects: startup deadline unused; Ctrl-C during seed/action lacks accepted-operation terminal outcome; non-ok JSON action receipt falsely reports success; cleanup clears owned handle before success and suppresses failures. [Report](w4-verification/report.md), [initial diff](w4-initial.diff). One focused repair/new verifier follows. F001 still open pending W4/W5/integration and W8.

### W4 focused repair — verified fixed (L2 managed lifecycle)

Fixer `/root/w4_repair`; fresh independent verifier `/root/w4_reverify`. Thirteen focused tests pass independently: real child/health/seed/cleanup, foreign listener exclusion, bounded late spawn/reap, accepted seed/action interruption terminal outcomes, receipt rejection, cleanup failure retaining owned handle and visible retry, and slow-drip request deadline. [Report](w4-reverification/report.md), [source hashes](w4-reverification/sha256.txt), [repair diff](w4-repair.diff). F001 managed lifecycle component verified at L2; W5 coordination/W7 integration/W8 target workflow remain. No hardware/L3 or carousel semantics claim.

### W5 initial verification — not fixed (scenario proof)

Fixer `/root/w5_fix`; independent verifier `/root/w5_verify`. Twenty-one focused and 114 host tests pass; scripted IDF build previously passed. Animated harness, managed serialized identity receipts and basic telemetry implemented. Concrete false proofs: action receipt arriving after snap_end replays earlier end and synthesizes effect; page-snap accepts unrelated/failed receipt. Push/removal verdicts use snap-end identity without actual applied revision evidence, and dispatcher lacks join/cancel lifecycle. [Independent report](w5-verification/report.txt), [source hashes](w5-verification/source-sha256.txt). One focused repair/new verifier follows. No firmware semantic repair or physical effect claim.

### W5 focused repair — not fixed; retry limit reached

Fixer `/root/w5_repair`; fresh independent verifier `/root/w5_reverify`. Twenty-three focused runtime tests, two native tests and 116 host tests pass; active scripted IDF build passes. Real managed idempotency/serialization verified. Actual coordinator still synthesizes false PASS from page_apply with another run ID (relabeled pending run) or no matching page_fetch; asynchronous dispatch data can mutate after submission. Verifier also notes the scenario dwell begins after snap_end, when new proof actions are rejected (actions accepted during the snap versus subsequent apply observation need careful distinction). [Full report](w5-reverification/report.txt), [source hashes](w5-reverification/source-sha256-current.txt), reproducible harnesses in same folder. W5 not accepted; planned one repair consumed. W7/W8 blocked pending authorized additional repair or resolved proof. W6 may continue independently on stable W2–W4 context/events. No physical or app semantic claim.

### Owner retry extension

Owner explicitly answered “Authorize one additional W5 repair” to the orchestrator's retry-limit question in this chat. This authorizes one further focused W5 repair followed by a fresh independent Luna verifier. Earlier not-fixed outcomes remain intact. The post-snap dwell is an observation window for an action accepted during the actual snap, not an authorization to start proof actions after snap_end. No additional carousel behavior repair is authorized.

### W5 owner-authorized extra repair — verified (L1/L2 host evidence)

Fixer `/root/w5_extra_fix`; fresh independent verifier `/root/w5_extra_verify`. Ten focused and 28 combined coordinator/server/paging/native checks pass. Original coordinator repros now emit zero effects and INCOMPLETE for foreign-run or missing-fetch apply; submitted mutable payload remains its original value. Positive integrated ingest test accepts an action during snap and validates matching fetch/apply during post-snap observation. Real managed loopback verifies idempotency and serialized revisions. [Report](w5-extra-verification/report.txt), [raw results](w5-extra-verification/raw-results.txt). Source SHA-256: scenarios `1a5bcb09eaa9290fc2c416241abcd15cae0786c55bb5bbbf4741b1bf2563bca1`; tests `589934ef18ac88952e581b82d7120076a1481c0389858706b4e87d897209739c`. W5 host evidence accepted; actual device semantic effects remain unmeasured until target scenarios, not inferred from these modeled traces. Earlier failures preserved. Evidence-only follow-up saved already-run results without tests/code changes. W6 begins; W7/W8 prerequisites now allow progress after W6.

### W6 implementation handoff — verification pending

Fixer `/root/w6_fix`. Added `scripts/lab02_evidence.py` as a standalone frozen-context/event-sink API for W7: safe single-segment run IDs, exclusive run-directory creation, whitelisted/redacted evidence context, whitelisted behavior flags, bounded thread-safe sanitized JSONL events, and atomic idempotent finalization. The caller supplies observed stage facts; `workflow_status=PASS` requires explicit completed-workflow and cleanup-success booleans. Experiment verdict remains independent. Unknown provenance stays null, and missing serial/metrics references stay null. No W7 invocation wiring or serial capture was added. Schema now defines partial manifests, nullable unknown provenance, two statuses, safe fields, and crash/event behavior.

Focused coverage includes normal completion with nominal `UNMEASURED`, preflight and build failure without a serial/parser artifact, Ctrl-C, cleanup failure, semantic FAIL with workflow PASS, secret redaction, unknown provenance, unsafe IDs/path references, concurrent events, PASS precondition and idempotent single terminal event. Command: `PYTHONPATH=scripts python3 -m unittest discover -s scripts/tests -p 'test_lab02_evidence.py' -v` — 4 tests passed. This is implementation self-check only; fresh independent verification is pending. No hardware or L3 claim.

Compact snapshot/raw command: `/tmp/lab02-w6-fix/`; source SHA-256: `scripts/lab02_evidence.py` `d4dfa9769ea1524f84726b336d1b4cf9256dc91c420d0db62e8b0dfce5b45258`, `scripts/tests/test_lab02_evidence.py` `54a822d61657673e063101f4ac1be57d29a07a63a3484708ae881e7131c9d724`, `docs/labs/schemas/run-manifest.v1.md` `cdf638e5977cee4bcdffb5de07b98868c49d02740ec2f6afaca21ae289429257`. No unrelated files were modified by this fixer.

### W6 initial verification — not fixed (writer invariants)

Fixer `/root/w6_fix`; fresh independent verifier `/root/w6_verify`. Four focused tests pass and source hashes match. Repro: max_events=2/eight events grows memory and JSONL to eight rows; atomic write failure caches final result before success so retry returns without a manifest. Relative artifact references can point outside intended lab run artifact roots. [Repro](w6-verification/repro.py), [hashes](w6-verification/current-sha256.txt). Safe IDs, nominal terminal idempotence, partial unknown fields and separate statuses verified at L1. Runner integration belongs W7, not a W6 defect. One focused writer repair/new verifier follows.

### W6 focused repair — not fixed (terminal row cap); retry limit reached

Fixer `/root/w6_repair`; fresh independent verifier `/root/w6_reverify`. Eight focused tests and independent original quota/atomicretry/path repros pass; concurrent overflow, sanitized partial updates and status separation pass. New concrete remaining boundary: 20,000-character terminal_reason produces a 20,094-byte terminal JSONL row, exceeding documented 16,384-byte maximum; manifest copies full reason. [Report/repro](w6-reverification/). W6 retry consumed; W7/W8 gated pending owner extension. This is a host writer cap boundary, not device/experiment evidence.

### Owner W6 extension

Owner explicitly answered “Authorize the W6 size-limit repair” in this chat. One additional focused terminal-row size repair followed by a fresh independent Luna verifier is authorized; earlier outcomes retained. Scope does not expand into renderer or firmware behavior.

### W6 owner-authorized extra repair — verified (L1/L2 standalone writer)

Fixer `/root/w6_extra_fix`; fresh independent verifier `/root/w6_extra_verify`. Ten writer tests and independent original quota/retry/path repros pass. Long ASCII/escaped/multibyte terminal reasons stay within 16,384 encoded bytes, preserve identical bounded redacted reason/explicit truncation in manifest/event, and atomic-write retry preserves one consistent terminal event. [Report](w6-extra-verification/report.md), [raw results](w6-extra-verification/raw-results.txt), [source hashes](w6-extra-verification/source-sha256.txt). GAP-001 writer component verified; actual complete/partial canonical run artifacts remain W7 integration and W8 target evidence. No physical claim. W7 begins.

### W7 implementation — independent verification pending

Fixer `/root/w7_fix` froze source; fresh independent Luna verifier `/root/w7_verify` dispatched. Canonical managed HTTP/manual/scripted wiring, explicit local/eager comparison evidence paths, controlled serial reset/readiness, redaction and partial-manifest integration implemented. Self-check: 70 focused host tests passed, including real owned loopback server with simulated UART; recycle IDF build exited 0 with explicit completion. HTTP/scripted build and L3 target workflow remain unverified. [Implementation snapshot](w7-implementation/w7-source-sha256.txt), [diff](w7-implementation/working-tree.diff), [tests](w7-implementation/focused-tests.txt). No hardware used or private configuration edited; no application semantic repair claim.

### W7 initial verification — not fixed (integration boundaries)

Fresh Luna verifier `/root/w7_verify`: 18 canonical/server and 16 host tests pass independently; command/help/registry checks pass. Confirmed integration gaps: capture log opens after reset; fresh readiness accepts an arbitrary build ID rather than matching the just-built image/harness; build callback rereads experiment settings after run context freeze; unterminated UART input buffer has no fixed byte cap. [Report](w7-verification/report.md), [source hashes](w7-verification/source-sha256.txt), [commands](w7-verification/commands.txt). Core managed lifecycle and targeting pass L2 simulated-host boundaries, but W7 is not accepted and W8 remains gated. One focused plan-authorized repair is dispatched to fresh Luna `/root/w7_repair`, followed by a different fresh verifier. No hardware or application semantics claim.

### W7 focused repair — fresh verification pending

Fixer `/root/w7_repair` froze source; different fresh Luna verifier `/root/w7_reverify` dispatched. Repair arms log/reader before reset, verifies per-run/source-bound build identity and exact harness completion fields, passes immutable experiment inputs to build overlays, and caps unterminated serial lines at 4096 bytes with partial evidence. Self-check 29 tests passed. Both manual `phaseb` and `scripted-phaseb` managed HTTP IDF builds exited 0; captured logs were secret-scanned. [Commands/results](w7-repair/commands-results.txt), [source hashes](w7-repair/source-sha256.txt). Build environment limitation: temporary `/tmp` sitecustomize shim directs denied psutil parent enumeration to ESP-IDF’s macOS os.getppid fallback; no installed dependency/repository configuration changed. Unshimmed canonical hardware execution is still W8 evidence. No hardware/private changes/application semantic repair. Planned W7 repair now consumed; acceptance awaits independent verification.

### W7 focused repair — not fixed; retry limit reached

Fresh independent Luna `/root/w7_reverify` reran 8 canonical tests successfully. Exact image/harness identity rejection and bounded serial input pass L1/L2; frozen build input call flow is source-confirmed, with adversarial configuration-mutation runtime evidence still absent from this verifier. Remaining reset-order gap: reader thread blocks on `reset_complete.wait()` until RTS deassertion; log/thread existence before reset does not establish active reading before reset. [Report](w7-reverification/report.md), [source hashes](w7-reverification/source-sha256.txt). W7 not accepted; W8 blocked on capture-order repair and fresh independent verification. Planned repair consumed. Root requested owner authorization for one additional focused capture-order repair; no additional edit or hardware run until answer. No L3/application semantic claim.

### Owner W7 capture-order extension

Owner explicitly authorized “one additional focused W7 capture-order repair and fresh independent verification for the reset/capture ordering gap” in this chat. Fresh Luna `/root/w7_capture_fix` is assigned the sole-reader/log-active-before-reset boundary and focused tests; a different fresh independent Luna verifier follows. Earlier not-fixed outcomes remain intact; no firmware semantics or private configuration change is authorized. W8 remains gated until verification passes.

### W7 owner-authorized capture repair — verification pending

Fixer `/root/w7_capture_fix` stopped edits; fresh independent Luna `/root/w7_capture_verify` dispatched. Sole UART reader completes an initial read before RTS; capture continues through reset, with each queued chunk carrying capture-phase metadata frozen at read return. Pre-reset data remains in the redacted raw log but cannot satisfy fresh readiness. Self-check: 10 focused tests passed, with affected tests rerun after interruption/phase-metadata changes; reset OSError and Ctrl-C preserve partial outcome and closed UART. Source hashes: runner `8a1770520f1809306c3b35d6e253caf5cffcd3be2e08a7ad85183748a3ae0ae6`; test `6bfba10d055a569d524fdf12d0a246eabf0d903931bf456eacc91b0c2b72211c`. Compact artifacts in `w7-capture-repair/`. No firmware/private configuration edits or hardware run. W8 remains gated until independent acceptance.

### W7 extra capture repair — not fixed (straddling-read false PASS)

Fresh independent Luna `/root/w7_capture_verify` passed 11 focused tests but reproduced actual-runner false workflow PASS from an identity-matching marker returned by a read that began before reset and finished after reset. Freshness was tagged at read return; the marker was classified post-reset. [Report/source hashes/repro details](w7-capture-verification/report.txt). No implementation or maintained documents changed by verifier. Owner-authorized extra repair consumed; W8 remains blocked. Root requested an additional focused read-start-phase repair plus fresh verification, without proceeding on dependent work.

### Owner W7 read-start extension

Owner explicitly authorized one additional focused W7 capture-order repair and fresh independent verification for read-start freshness. Fresh Luna `/root/w7_readstart_fix` is assigned the straddling-read false PASS and targeted regression coverage only. Fresh independent verification follows; earlier failed outcomes remain. W8 stays gated until acceptance.

### W7 owner-authorized read-start repair — verification pending

Fixer `/root/w7_readstart_fix` stopped edits; fresh independent Luna `/root/w7_readstart_verify` dispatched. Read phase is frozen before `conn.read()`, so a pre-reset read returning after reset cannot establish freshness. Self-check 13/13 passed with loopback access: straddling marker retained in serial log but INCOMPLETE; subsequent post-edge read PASS/UNMEASURED; active-before-reset, reset error and interruption cleanup remain passing. Source hashes: runner `56723b97c1503da6afeb4806fb3c52644389c23eca401bbe35424c9105417a08`; tests `8ad34022322c8fdd0d91e7da53a9334b0589c47dca6ce00b59bf6ad5238dab3d`. Compact artifacts in `w7-readstart-repair/`. Host-only change; no hardware or new firmware build. W8 remains gated.

### W7 read-start repair — verified fixed (L2 simulated host)

Fresh independent Luna `/root/w7_readstart_verify` confirmed matching source hashes and 13/13 focused tests. Original frozen runner independently returned false PASS for the new straddling-read regression; repaired runner returns INCOMPLETE/fresh_boot_marker_timeout while preserving the marker, and subsequent post-edge read yields PASS/UNMEASURED. [Report](w7-readstart-verification/report.txt), [hashes](w7-readstart-verification/source-sha256.txt). W7 capture-order boundary accepted at simulated-host L2; no hardware/L3 claim. Adjacent checks 48/49: only failure is stale mock expectation in build-only host test omitting optional `build_id=''`; production behavior failure not demonstrated. Test-only expectation maintenance follows, then integrated checks and W8. Previous not-fixed history preserved.

### W7 integrated gate — accepted (L2 host); W8 begins

Fresh independent Luna `/root/w7_integration_verify` passed the full 146-test host suite, registry/deployment validators, help and both canonical dry-runs. Test-only maintenance adds the optional empty build_id to an obsolete mock expectation and preserves the no-USB-enumeration guard; 16 host tests pass. [Integrated report/current hashes](w7-integrated-verification/report.txt). W7 accepted at host L2; no device claim. Existing cleanup test removed phaseb build, so W8 requires fresh build. Exactly one registered kit is connected. Fresh Luna `/root/w8_execute` assigned actual scripted canonical workflow then coordinated manual capture/owner touch, followed by independent fresh verification. All firmware/host source frozen; no private edits/commits or app behavior fixes.

### W8 physical execution — independent verification pending

Fresh Luna driver `/root/w8_execute` ran actual unshimmed canonical commands without KIT/PORT overrides against exactly one registered kit. Scripted run `20261012-w8-scripted-01`: build/flash/fresh identity readiness completed, then harness reported inbox_not_ready; terminal FAIL/UNMEASURED with harness_failed. Manual run `20261012-w8-manual-01`: full 120s capture, fresh readiness and cleanup; terminal PASS/UNMEASURED. Both owned server lifecycles ended and no listener remained. [Driver report](w8-execution/lab02-w8-execute-report.txt). Fresh independent Luna `/root/w8_verify` assigned artifact verification; no final L3 disposition yet.

Owner observation during announced manual capture: top showed “inbox unavailable”, main showed “loading inbox”; owner confirmed swiping kept loading/unavailable. Recorded as application behavior, not inferred from serial and not repaired. At owner request, endpoint reported as 192.168.8.177:8765 and station address 192.168.8.160. Wi-Fi timeout/fetch_fail observations do not establish a root cause. No source/private configuration edits or retries; hardware stopped.

### W8 independent physical evidence verification

Fresh Luna `/root/w8_verify` independently confirmed actual unshimmed builds and flashes, distinct run/source-bound image identities, one registered target reused without KIT/PORT, fresh matching readiness, full 120s captures, managed health/seed/cleanup receipts, zero event drops and no remaining listener. Manual PASS/UNMEASURED supports host L3; owner-observed loading/unavailable and fetch_fail do not establish application success. Scripted FAIL/UNMEASURED/harness_failed records inbox_not_ready terminal abort; verifier flagged early-terminal telemetry versus successful-completion parser format for status-contract clarification. [Original independent report](w8-verification/report.txt), [source/artifact hashes](w8-verification/source-and-artifact-hashes.txt). Root requested evidence-only normative disposition before any further change: incomplete scripted traversal can be a host-stage failure while app cause remains separate; no speculative status repair is authorized by a plausible interpretation alone. Source remains frozen; no further hardware.

### W8 clarification and final scoped dispositions

Independent `/root/w8_verify` withdrew its tentative workflow-status concern after checking the required scripted traversal gate. Scripted FAIL is justified because traversal aborted before expected steps; this is not a confirmed host-status defect. [Clarification](w8-verification/status-clarification.md) preserves the correction alongside the original report.

W1–W7 are independently accepted; the integrated host suite passed 146 tests and both validators. W8 physical validation and owner touch observation are complete: manual host PASS/UNMEASURED, scripted host FAIL/UNMEASURED with inbox_not_ready. Both commands ran unshimmed, built/flashed matching run-bound images, froze one registered target, captured fresh reset/readiness and 120s logs, and cleaned up the owned server. No claim that the scripted scenario or inbox application works is made. Source remains uncommitted on the original HEAD; no application behavior repair or further private edit occurred.

| Item | Final disposition / evidence |
|---|---|
| LAB02FLOW-F001 | Verified fixed for owned host workflow: lifecycle/actions/scenario evidence L2; actual manual and failure-reporting scripted lifecycle L3. Scripted traversal remains an observed application failure, safely reported FAIL. |
| LAB02FLOW-F002 | Verified fixed L1/L2; exact URL recovery/copy corroborated by recorded hashes. Original private pre-migration bytes were not archived, so independent actual before/after byte comparison remains unavailable. |
| LAB02FLOW-F003 | Verified fixed L1/L2 adverse targeting; L3 confirms one registered target selected before build and consistently reused in both physical commands without KIT/PORT overrides. |
| GAP-001 | Full/partial evidence writer and integration verified L1/L2; actual manifests/events/metrics/capture references verified on target. Unknown provenance stays explicitly null. |
| LAB02APP-F001 | Deferred observed inbox availability failure, with owner touch and serial evidence; root cause unproven and no app repair authorized. [Follow-up](application-followup.md). |

Physical push/removal/page-snap semantic effects remain unmeasured: W5 verifies host receipt/identity proof boundaries, not firmware semantic success. Existing local/eager comparisons are host-verified, not physically rerun. No commits were made. The connected kit is idle and no managed listener remains.

### Final shared-workspace private provenance check

The current private deployment SHA-256 differs from the saved post-W3 digest; its modification time is 2026-10-13 01:51 UTC (2026-10-12 20:51 Central), before the W8 runs. No workflow agent reported a private edit after W3; attribution is unknown and owner clarification was requested. The file was left intact. The obsolete field remains absent. [Hash-only record](private-provenance-final.json). This does not retroactively supply the missing original migration byte archive or establish unchanged private bytes through final delivery. W8 used the then-current validated private inputs; original migration evidence and limitations remain historical.

Owner clarification: “I did. I edited it. I removed the lab entry top-level key.” The final hash change is attributed to this owner-reported post-W3 edit; current bytes remain preserved. No independent pre/post byte comparison of that owner edit is claimed.
