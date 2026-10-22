<!-- orchestrate-build:begin run_id=20261020-lab11-sw-001 -->
# Orchestration progress

- Run ID: `20261020-lab11-sw-001`
- Status: complete
- Map: `docs/labs/remaining-labs-implementation-plan.md`
- Map fingerprint: `14bf4e82114fb17789c7e8837082d274f9c54aab` (git hash-object of the plan file)
- Selected section: 3.7 LAB-11, plus sections 1, 2, and 8
- Specification: section 3.7 together with `docs/product/communication-contracts/message-delivery.md`, `docs/labs/v2-experience-demo-roadmap.md` LAB-11, and `docs/standards/client-application-coding-standards.md`
- Repository root for edits: `C:\Users\lynnv\src\family-link-lab11-wt`
- Shared checkout (do not merge into): `C:\Users\lynnv\src\family-link-lab05`
- Branch: `lab11-send-receipt` (local worktree only; no commit, push, or merge authorized)
- Baseline revision: `4f1011e7b9736aedefbcb3bedb65131a629c478f`
- LAB-10 manifest fingerprint: `ac17188a0c8346732e93873a02c047e607f64655`
- LAB-10 `capture_policy` prerequisite: `accepted_point_cap` 4096, `duration_cap_ms` 180000, `queue.capacity` 16. Read from the manifest. Not a LAB-10 experiment verdict.
- Execution scope: software (`L11-MODEL`, `L11-FW`, `L11-HOST`, `L11-QUAL`). `L11-DEVICE` not selected.
- Dispatch override: one integrated implementer, then one fresh independent read-only reviewer for standards review and verification of all four packages. One grouped repair and one fresh recheck are allowed. Hard maximum 4 invocations. Subagents must not delegate.
- Auto-continue: true (user authorized run now)
- Started: 2026-10-20
- Updated: 2026-10-20T16:08-05:00

## Baseline protected paths

Shared checkout `C:\Users\lynnv\src\family-link-lab05` was clean at dispatch. Do not edit it.

Worktree checkout produced CRLF-only modifications (equal insertions and deletions). Do not revert, normalize, or rewrite them:

- `poc-v1/scripts/monitor_com4.py`
- `scripts/lab02_device_capture.py`
- `scripts/tests/test_lab02_host.py`
- `scripts/tests/test_lab_registry.py`
- `server/labs/README.md`

Other lanes own LAB-07, LAB-08, LAB-09, and LAB-12. Do not modify those trees, adapters, evidence, or registry blocks.

## Visible controls frozen before implementation

Static layout. Progress is Level 0 text on the existing scene. The scene root is not recreated for part, receipt, or failure updates.

The screen shows the active fixture acceptance condition (`accept-on-archive-commit` or `accept-on-recipient-publish`) and the expected result for the selected scenario. Success text appears only after that condition. The product word Sent is not an approved outcome (`COM-OPEN-001` stays OPEN).

Decided scenarios and expected results:

| Control | Expected result |
|---|---|
| D-one | One recipient. Client `terminal=success` if and only if server `accepted=1` under the active condition. |
| D-drop-ack | Retry of the same part sequence does not increase the durable part count. Different bytes for that sequence are `identity_conflict` and do not overwrite. |
| D-drop-receipt | Lost complete response, then lookup by the frozen client message id. Server accepted count stays 1. |
| D-full | One `failure` / `storage`, `live_buffers=0`. |
| D-cancel | Four operation ids. Each `terminal_count` is 1. Before `accepted=1`, `terminal=cancelled`. After `accepted=1`, server `accepted` stays 1. |

Exploratory controls stay labeled and out of `experiment_inputs`: D-many, D-invalid-recipient, D-offline (`work` copied from `offline-probe.json`), D-push-fail (receipt and push are separate fields), D-restart (status and resumed sequence ids only).

Chunk probes are 1024 and 4096 bytes. Part queue capacity is 8. Overflow is one `failure` / `overflow`. None of these approve a product chunk size, outbox, recipient atomicity, or wire format.

Payload is an opaque synthetic frozen capture. `OPEN-SKP-03: OPEN`. Do not link or modify LAB-10 firmware.

## Package ledger

| Package | Title | Dependencies | Implement | Verify | Repair attempts | State / evidence |
|---|---|---|---|---|---:|---|
| L11-MODEL | Frozen capture, part/receipt model, fixture server | LAB-10 capture_policy fields | completed | pass | 0 | Independent: 19 native/host tests; frozen hash matches; fixture part/receipt behavior observed |
| L11-FW | Device owner, Level 0 progress, cancel phases | L11-MODEL | completed | pass | 0 | Independent: both sdkconfigs and ELFs match claimed build ids; no rebuild required |
| L11-HOST | Parser, adapter, registry on port 8770 | L11-FW | completed | pass | 0 | Independent: registry validate exit 0; workflow PASS mechanism is host-only |
| L11-QUAL | Report, standards review, software qualification | L11-HOST | completed | pass | 0 | Independent: `make test` 440 passed, 5 skipped. Software qualified. Device rows blocked |
| L11-DEVICE | Target execution | L11-QUAL | not_selected | not_selected | 0 | Not selected. L11-06 L2 and L11-07 L3 pending |

## Handoff supplement (same implementer invocation; not a new review cycle)

Passed to the running implementer before code was committed to a design. The independent reviewer must check this supplement together with the original acceptance criteria. Do not implement the future live-capture integration in this run.

1. Synthetic capture stays outside send-operation logic. Immutable payload and recipients have explicit ownership and lifetime: transfer or borrow, who releases, and success, failure, cancellation, and stale completion. LAB-10 is not linked or modified.
2. Firmware-used implementation performs real part uploads and receipt/status requests against the fixture. Scripted success depends on actual parsed responses. Client outcomes reconcile with server durable part/message counts and the active acceptance condition.
3. Lost acknowledgements and lost accepted receipts are tested at those boundaries. Retries keep immutable operation and payload identity. Duplicate processing does not create duplicate delivery within the fixture's declared semantics. Local timeout or cancellation is not presented as remote rollback.
4. Allocation and copying are inspected for both chunk sizes. Resource accounting includes retained input, staging, transport, retry, and response buffers. A chunked upload that first duplicates the complete recording does not count as bounded streaming. Calculated bounds, host observations, and pending device measurements stay distinct.
5. Progress updates and cancel input do not wait for upload, receipt lookup, or worker cleanup. Late responses after cancellation and queue pressure have explicit resource cleanup and exactly one local terminal outcome.

Report addition: a concise handoff section covering the reusable send interface, payload ownership, peak-memory contributors, and the future live capture → stop → upload → receipt scenario required before end-to-end qualification. Product Sent meaning, outbox retention, recipient atomicity, and wire choices stay OPEN.

## Invocations

1. Implementer: completed (`296eb568-65c5-41ac-82fa-10b040327538`). Status completed. commit_created false. Full `make test` claimed 440 passed, 5 skipped. Both firmware builds claimed. Shared checkout remained clean. Protected CRLF files have no non-whitespace diff. Scope matches LAB-11 plus surgical registry, schema, allowlist, Makefile, and evidence index.
2. Independent reviewer: pass (`b66a69df-2824-4850-9838-dddac2047bcd`). Read-only. commit_created false. All four software packages pass. Supplement accepted. No repair. Device evidence remains pending.

## Package L11 software qualification

- Definition fingerprint: plan `14bf4e82114fb17789c7e8837082d274f9c54aab`; LAB-10 manifest `ac17188a0c8346732e93873a02c047e607f64655`
- Implementer: [LAB-11 implementer](296eb568-65c5-41ac-82fa-10b040327538), one invocation including the handoff supplement
- Verifier: [LAB-11 reviewer](b66a69df-2824-4850-9838-dddac2047bcd), fresh read-only invocation
- Verifier verdict: pass
- Checks actually rerun by the reviewer: `python -m unittest scripts.tests.test_lab11_model scripts.tests.test_lab11_host` (19 tests, exit 0); `python scripts/lab_registry.py validate` (exit 0); frozen-file SHA-256 `c2f881f1b4c1a99bc7b3112acadf2e8588ccae36fd975a11e05f941b93ef39e1`; sdkconfig inspection, no rebuild; `make test` (440 passed, 5 skipped, exit 0)
- Residual notes outside the verdict: P2 label-string race in `lab11_fw.c` `tick_cb` (device observation still required); P3 README line 53 names the UI task as the terminal writer. The harness task applies terminals.
- Final state: software qualified; device evidence pending. `L11-DEVICE` not selected.

<!-- orchestrate-build:end run_id=20261020-lab11-sw-001 -->
