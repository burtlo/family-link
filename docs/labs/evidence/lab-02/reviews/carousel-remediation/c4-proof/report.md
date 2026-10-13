# C4 proof-first identity review

Verdict: **one confirmed P1 defect**, temporary label **C4-SURVIVING-ANCHOR**. No repair attempted. Production session/controller/card code and actual vendored LVGL reproduce silent replacement of an already viewed surviving message after a newer list revision shifts that identity outside the locally loaded pages. Root assigns a stable ID and a separate focused fix/verification assignment.

## Boundary and authority

Read-only assigned C4 review of `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox`, main HEAD `7ecc594861c1e6fbc9ad647ac591bb44df3acde9`, all current staged/unstaged/untracked source WIP included. Source frozen during assignment. Full canonical client-code-review skill/workflow, docs/AGENTS.md, client coding standard, implementation practices, remediation CAR/C4 criteria, review.md, execution.md APP-O-01–05, and f008-verification/report.md were inspected. No implementation or maintained-doc edits, index changes, hardware, flash, host-workflow or private-configuration changes. Harness and review artifacts confined to `/tmp/lab02-c4-proof`.

Normative standard §5 preserves identity on bounded Level1 updates. Authorized lab CAR-PAGING-5 explicitly preserves the viewed message on newer revision when present; CAR-BIND-6 coherent snapshot and CAR-INSERT-REMOVE-3 support coherent applied revision/focus/window. Workflow O-04 and APP-O-02 govern viewed-ID preservation/deferred motion; APP-O-03 chooses next after viewed removal and previous if last. Draft product-wide paging policy and numeric gates remain OPEN. This finding uses an already materialized, visibly bound stable viewed anchor; it does not decide what an unknown ID outside any materialized window means.

## Ownership and wait map

Production carousel intent → production session focus + immutable fetch admission → adapter consumes actual request → pthread completion publishes actual terminal mailbox → production session pump applies newer page0/model and reconciles identity → production carousel pump updates count/focus/scroll and binds bounded card subtrees → actual LVGL renders saved PPM. Network and task transport are adapters, never a simulated Python client reducer. Server-unit pages in the correlated run go through the production C v2 parser. Session remains the writer; this is a incorrect reconciliation invariant, not a concurrent-writer claim.

## C4-SURVIVING-ANCHOR

- Classification/priority: **confirmed defect / P1** (silent loss of viewed identity after a reachable valid remote revision).
- Tight location: `/Users/lynnfrank/src/family-link/client/labs/lab-02-bounded-inbox/components/lab02/lab02_session.c:249–253`, `reconcile_focus_id`. Related reset/clamp sites at 277–284 and 322–328 establish why the old anchor is outside the newly loaded subset; they are not separate findings.
- Exact source SHA256: `6a95f5a74c7bf5717dab05227b79df5f85cda5f65c10ec0b0111b236e72c1904`. Complete source/document/script/test provenance in before-sha256.json/after-sha256.json.
- Trigger: initialize valid256-item/page16 inbox rev1; focus200, deliver its actual admitted pages, visibly bind m200. Before next refresh, remove m0…m99 through100 serial identity-targeted server actions; m200 survives at index100 in revision101/count156. Accept newer page0 through actual refresh operation. Production clamps old200 to155 and requests new pages9/8; neither contains m200 at page6. Complete those actual requests.
- Effect: current+adjacent loaded-page predicate incorrectly proves global absence and replaces retained anchor with m255. Session/current card focus settles155, applied revision101, live visible IDs m253/m254/m255, viewport scroll22156 (before scroll28718 with m199/m200/m201). Explicit later delivery of page6 containing m200 leaves focus155/m255: the stable anchor has already been discarded. No user focus intent intervenes.
- Violated requirement: CAR-PAGING-5 and standard §5 viewed-ID preservation. CAR-BIND-6/CAR-INSERT-REMOVE-3 require the published coherent state to carry the correct identity; this run demonstrates a coherently rendered but semantically wrong identity. CAR-INSERT-REMOVE-1 and O-04 provide same preservation principle, though the exact trigger is cumulative nonfocused removals, not an appended push or removal of the viewed item itself.
- Reachability: actual lab fixture server preserves list order on remove_at, deletes exactly the supplied identity in remove_message, increments revision each time, and exposes offset slices.100 actions are below action ledger capacity256, total256 and page16 are supported existing lab bounds. This needs neither fabricated head insertion nor a new product-wide policy. UI remains idle between accepted mutations and refresh. Server unit receipts prove acceptance, actual C page_apply/fetch_terminal prove application, actual LVGL state/PPM prove visible effect; receipt alone is never called a client PASS.
- Smallest fix (advisory): retain the known viewed-ID anchor until it is located in the new revision or its absence is established with a complete bounded reconciliation proof. Fetch/search remaining revision pages incrementally within existing lab256/page16 limits, or use another equally sound bounded identity-resolution mechanism. Only then select next/previous when genuinely removed. Preserve latest user intent, mutation deferral and normal terminal/admission rules. Do not implement a speculative product paging policy or eager widget rebuild.
- Verification: original256→156 survivor displacement must end m200/index100 and correct bound window/revision. Boundary genuinely removed viewed item must still select next, previous if last; latest intent while deferred revision applies must win; stale old-revision completion cannot revert anchor. Existing nominal append/removal cases remain required counterevidence. Fresh independent verifier needed after implementation.
- Required evidence: L1 actual session/reconciliation/operation cases; L2 actual LVGL scripted identity/bind/viewport trace plus managed server receipt→applied→visible correlation; L3 representative physical target UI/mutation/touch/lifecycle confirmation. No L3 waiver.
- Dependencies: current C1 mailbox and accepted C3 gesture/snapshot mechanisms are existing prerequisites at L1/L2; F008 initial-retry mechanism independently verified L1/L2. No new ordering prerequisite for this focused identity repair. Fresh independent verification follows fixer completion. C5/C6 resource/numeric/target evidence remains separate and open.

## Proof artifacts and observations

proof.c is a temporary extension including the unchanged existing runtime harness with main renamed, sharing its production request/completion, pixel capture and window invariant helpers. Initial harness drained request API until false, but production take_fetch_request uses portMAX_DELAY and blocks when empty. The scratch drain was corrected to check existing adapter queue depth first; no production change. Build succeeds (existing LVGL deprecation warnings). proof.log records admitted operations1–9, delivery, apply and exactly one fetch_terminal for completed operations; loading surfaces stay represented. Revision-applied checkpoint temporarily shows loading at155, followed by wrong materialized m255 after pages9/8 complete. Relevant page ordering is initial0/1, old12/11/13, refresh0, new9/8, then explicit6. Loaded-page evidence is obtained from those actual deliveries, not an exposed session bitset.

proof-server.c runs the same case with pages generated by production InboxState.page, parsed by production lab02_inbox_model_parse_v2_page. server_semantics.py directly instantiates the unchanged production server unit with a valid m0…m255 fixture and performs100 actual action(remove_message) calls. server-receipts.json records each accepted action/revision; server-pages retains exact page_receipt_id/run_id/timestamps, and proof-server.log carries those through actual page_apply. It confirms the same m255 result even after m200 page arrives. This is a local server-unit/transport adapter correlation, **not** real HTTP transport, a canonical managed workflow or physical target confirmation. No server/network commands were started after root requested report-only freeze.

Saved frames: surviving-id-displacement-viewed-m200.ppm, surviving-id-displacement-revision-applied.ppm, surviving-id-displacement-neighbor-pages-loaded.ppm, surviving-id-displacement-surviving-page-eventually-loaded.ppm, plus equivalent server-surviving-id frames. Continuous window assertions and trace establish viewport/binding state; no target screen observation claimed.

## Nominal counterevidence and residual gaps

No broad suite was rerun. Assessed current-source F008 independent normal logs: push-during-snap retains m8/rev2 at8; focused removal selects m9/rev2 at8; mutation replacement selects latest m6 relocated7 (push model) or next m7 at6 (removal); reversal selects latest m2/rev4 at2. Each log reports PASS with snap starts=ends and coherent matching bound card state. These nominal results do not establish global ID absence when survivors shift beyond adjacent loaded pages. Previous remove-last evidence is existing source/evidence counterevidence, not rerun here. Cold unmaterialized focus policy is not a newly confirmed defect.

Managed-server client/device receipt→visible confirmation and representative target mutation, touch/animation timing, RTOS concurrency, memory/stack/largest-block remain missing. C4 is not fully accepted; C5/C6 numeric gates remain OPEN; L3 is not waived. Historical draw-buffer50 choice and current sdkconfig50 are preserved; no comparison-height claim.

## Commands actually executed

Read-only rg/cat/sed/nl inspected the sources above and current runtime/server tests. Python subprocess snapshots captured `git status --porcelain=v1`, `git rev-parse HEAD`, `git branch --show-current`, `git diff HEAD --binary`, `git diff --cached --binary`, then SHA256 of1939 source/maintained-document/test/script files. Scratch generation wrote only /tmp harness/CMake/report artifacts.

Build/run commands:

```
cmake -S /tmp/lab02-c4-proof -B /tmp/lab02-c4-proof/build -DCJSON_DIR=/Users/lynnfrank/esp/esp-idf/components/json/cJSON
cmake --build /tmp/lab02-c4-proof/build -j 8
/tmp/lab02-c4-proof/build/lab02_c0 surviving-id-displacement /tmp/lab02-c4-proof
python3 /tmp/lab02-c4-proof/server_semantics.py
cmake -S /tmp/lab02-c4-proof -B /tmp/lab02-c4-proof/build -DCJSON_DIR=/Users/lynnfrank/esp/esp-idf/components/json/cJSON
cmake --build /tmp/lab02-c4-proof/build -j 8
/tmp/lab02-c4-proof/build/lab02_server_proof server-surviving-id /tmp/lab02-c4-proof
```

Raw configure.log/build.log/proof.log and configure-server.log/build-server.log/proof-server.log/server-semantics.log retain outcomes. Initial blocking drain run was superseded by corrected scratch run; attempted ps/pkill process inspection was denied by sandbox (no process mutation succeeded). This is harness API correction, not product failure evidence.

## Preservation and outcomes

preservation.json confirms branch, HEAD, full porcelain status, complete tracked HEAD/WIP binary diff and staged-index binary diff byte-identical before/after; all1939 hashed source/docs/test/script files identical. `before-index`/`after-index` mean **index contents via git diff --cached**, not a claim that raw .git/index metadata bytes were separately snapshotted. Full before/after bytes retained, no add/reset/stash/commit performed. Hashes:

- Full status:63b3c3d0504b6700f38d92dcd8f18c4c0717596135822600176509a69360fbd9
- Full tracked HEAD/WIP diff:bd93b037b0ad2afa7177f328776b10806b97a5e2284218deb2610e2295ce46be
- Staged/index contents diff:1c9da5cab9563739b8a3b7821f8c08defbde68c0cbc45866d267e6d50ff386d2

C4-SURVIVING-ANCHOR confirmed by L1 production reducer behavior and L2 actual LVGL scripted runtime. L0 scratch compilation passes; no additional firmware build needed for proof. No fixes attempted. Root owns stable ID, repair assignment and maintained report updates. Packet frozen after report creation.
