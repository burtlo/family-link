# Plan: Correct and qualify h34 message_store

| Field | Value |
|---|---|
| **Doc kind** | `island-demo-correction` |
| **Status** | `planned` |
| **Last updated** | 2026-10-04 |
| **Scope** | Host-only PCM implementation in `demos/server/h34_message_store/` and its evidence/docs |

## Goal

Bring the h34 host demo into conformance with the durable message protocol and filesystem contract before relying on it as the Phase 3 server proof. Close the authorization, finalization recovery, durability, bounded streaming, trash recovery, exact-selection, and validation gaps. Replace the current short happy-path claim with reproducible 180-second PCM and fault-injection evidence.

This plan does not authorize firmware/X02 integration, Opus work, production deployment, or hardware qualification. The existing H32 changes in the workspace are outside this plan.

## Current evidence and gaps

The current client covers short PCM chunks, duplicate/conflicting chunk responses, one happy-path completion, one ordinary restart with an open upload, one ordinary restart with a completed message, Range playback, and cull/restore without a restart. It does not establish crash consistency at finalization boundaries, process-level admin authorization, restart-safe trash, a three-minute transfer, or bounded peak memory. The current finalizer persists `finalizing` before work and treats that state as busy after restart; completed-message completion returns before checking sender ownership; administrator handlers accept any known device; Range handling reads the full selected range; startup does not index trash; and cull preview does not evaluate `completed_before` or bind a normalized exact selection to its caller.

The roadmap currently marks Phase 3 done while acknowledging the missing three-minute fixture and trash-index gap. Keep that status as `in progress`/`blocked on correction` until every gate below passes; do not use the existing short demo as proof of the missing properties.

## Phases and acceptance gates

### Phase A — Define identity, authorization, and validation contract

**Work**

- Check sender ownership before every completion-state shortcut. Define completion idempotency: an authorized original sender repeating the same completion tuple receives the existing completed result; a different sender is forbidden, and a conflicting tuple receives a conflict response without mutation.
- Introduce an explicit administrator authorization check based on the configured registry role (`role == admin`) or a separately documented administrator credential source. Device authentication alone is insufficient. Apply it to preview, cull, trash inspection, restore, and purge if purge is exposed. Bind a preview token to the authenticated administrator and make it single-use and expiry-bounded.
- Define strict request validation for message IDs, sequence range, lowercase/hex SHA-256 values, nonnegative declared byte counts, supported PCM metadata, target/chunk timing, monotonically contiguous chunk time ranges, total duration, aggregate bytes, recipient/broadcast rules, completion chunk count, completion duration, and closed reason. Reject inconsistent or unknown unsupported media metadata before accepting storage. Bound concurrent writes and open-message count or record these as an explicit out-of-scope demo limit rather than implying the general storage contract is complete.
- Enforce the normative limits: 180,000 ms, 6 MiB encoded PCM, 192 KiB per chunk, 16 kHz mono PCM S16LE. Ensure upload-time totals and completion-time totals agree. Validate range syntax and bounds and define the behavior for malformed, unsatisfiable, and suffix byte ranges.

**Gate A**

- Tests prove a recipient can read authorized completed media but cannot complete it; an unrelated authenticated device cannot complete it or receive its manifest; the original sender can repeat an identical completion; altered completion metadata does not change the completed message.
- A child-role credential is rejected on every admin route. An admin credential can preview/cull/restore. Tokens cannot be used by another admin or replayed after use/expiry.
- Boundary and malformed-input cases return documented status codes and leave no committed partial state. Valid PCM metadata and timing meet protocol limits.

### Phase B — Make finalization crash-recoverable and storage acknowledgements durable

**Work**

- Specify finalization as recoverable, idempotent disk phases. Persist a validated completion intent (including the caller-supplied completion tuple and source hash) before materialization. On startup, reconcile each incoming state by inspecting and validating artifacts; resume safe work or return it to `open` only when no committed completion state exists. Never leave a permanent `finalizing` busy state.
- Make completed-message publication atomic: write WAV and completed manifest via temporary files, flush file contents, atomically replace, write/flush the completion marker last, flush the incoming directory, rename the whole directory, then flush source and destination parent directories. Define platform-specific handling where directory fsync is unavailable and do not claim stronger guarantees there.
- Make every acknowledged durable chunk follow: write temp, flush and `fsync`, checksum/size validation, atomic rename, `fsync` containing directory, atomically replace and `fsync` manifest (or use a journal with equivalent recovery), then acknowledge. Apply the same durable-write discipline to completion intent, inbox journal, cull deletion record, audit record, and restore/cull directory moves.
- Make inbox publication replay-safe. Recovery reconciles completed canonical messages against recipient references and appends missing references exactly once, including broadcast recipients. A crash between message publication and inbox updates must not create duplicate references on each restart.
- Preserve source chunks until canonical media and completed metadata are committed. Cleanup is a later idempotent phase; a crash during cleanup must not invalidate the canonical message.

**Gate B**

- Fault injection after every durable operation in the finalization matrix below, followed by a fresh process start, converges to exactly one valid state: resumable `open`, recoverably `finalizing`, `complete`, or quarantined with an actionable diagnostic. No committed upload is permanently stranded and no incomplete directory is exposed as complete.
- A completed message is indexed once, has exactly one completion marker, hashes verify, and each intended recipient has exactly one inbox reference after any injected crash/restart sequence.
- Durable acknowledgement claims are backed by explicit flush/fsync ordering. If the host filesystem cannot provide a tested power-loss guarantee, documentation labels the evidence as process-crash durability only.

### Phase C — Bound media reads and recover trash exactly

**Work**

- Replace whole-range reads with a bounded file iterator/streaming response; bounded resident memory must not scale with requested range length. Keep ordinary full-file responses streaming. Return correct `206`, `Content-Range`, `Content-Length`, `ETag`, and media type; return `416` with `Content-Range: bytes */size` for syntactically valid unsatisfiable ranges. Authorize and validate the completion marker and media path before opening content.
- Specify whether PCM range boundaries must align to complete samples (including the WAV header offset). Reject a range that would split a sample, or document a deliberate container-level byte-range policy and test it.
- Rebuild the trash index at startup by scanning `trash/**/deletion.json`, verifying message identity and manifest/media hashes, and rejecting path traversal. Restore works after restart and atomically returns the message to its canonical month directory. Keep trashed messages out of inbox/media routes, and define whether restore reactivates existing inbox references or reconstructs them.
- Calculate preview from a canonical normalized selector. `completed_before` must be parsed and applied; duplicate/invalid IDs and selector combinations must have deterministic behavior. Return exact IDs, per-message bytes, total bytes, and affected inbox references. Store that exact selection, selector, actor, expiry, and relevant manifest identity in the single-use token. Cull must revalidate the snapshot and report stale/conflicting items rather than silently culling a changed set.
- During cull, atomically move the complete directory into trash and persist its deletion metadata. Make the move and audit recoverable across crashes. Do not claim media deletion as part of a cull; trash remains recoverable.

**Gate C**

- A large full-range request and small ranged requests are streamed with bounded chunks; response headers and bytes match the selected interval. Authorization failures do not open/read media. Invalid and unsatisfiable ranges return the specified error.
- A preview filtered by timestamp returns exactly matching eligible messages and bytes. Replaying the token, using it as a different admin, or mutating a selected message between preview and cull cannot silently apply a different selection.
- Cull, process restart, trash enumeration, restore, and another restart preserve hashes and the agreed inbox behavior. A deliberately interrupted move recovers without duplicate or missing canonical directories.

### Phase D — Hermetic 180-second fixture and corrected evidence/status

**Work**

- Add a hermetic fixture generator/harness that creates exactly 180,000 ms of deterministic 16 kHz mono signed-16-bit PCM (5,760,000 raw audio bytes; canonical WAV is 5,760,044 bytes), split into protocol-valid chunks at a documented interval. Generate samples on demand or into a temporary directory; do not check in a multi-megabyte opaque fixture. Use an isolated temporary `FAMILY_LINK_ROOT`, temporary registry with sender/recipient/admin/ordinary-device credentials, ephemeral localhost port, and no user data or network dependency beyond loopback.
- Exercise create, sequential and reordered upload as appropriate, checksum/dedup validation, completion, recipient inbox/read, full streaming GET, multiple Range requests, and restart recovery. Include a repeatable memory measurement for upload, finalization, and Range serving. Record Python allocation peak and process RSS (when available), measurement method, runtime/version, payload size, and chunk size. Acceptance is no allocation proportional to the complete payload and a measured peak below 1 MiB above a warmed baseline for server-side Python allocations, with RSS reported separately rather than used as the sole pass criterion. If framework/runtime overhead makes the proposed threshold invalid, document a justified threshold before claiming a pass.
- Keep fault-injection cases separate from the fast demo command if they are slow, but provide one documented command for the complete qualification run and machine-readable output (JSON) alongside a concise human-readable summary.
- Update `docs/evidence/h34-message-store/README.md` to list exact commands, fixtures, fault cases, limits, environment, hashes, measured memory, and known limitations. Correct `docs/plans/long-message-experiments.md` Phase 3 status to `todo`/`in progress` until all gates pass; then mark done only with links to the new evidence. Update `docs/plans/message-store-demo.md` so its status and remaining work match observed evidence, and link this correction plan.

**Gate D**

- Clean checkout command produces the full 180-second result without relying on preexisting store data or credentials. The reported PCM and WAV lengths, source/media hashes, chunk count, inbox count, and response-range hashes are deterministic.
- Memory evidence meets Gate D's bounded-allocation criterion and demonstrates the same result for a small range and a full-range response. The measured full-payload size exceeds the server-side Python allocation peak by the documented margin.
- Evidence output is retained, reproducible, and sufficient for another engineer to distinguish pass, fail, and unsupported host durability guarantees. The Phase 3 roadmap status accurately reflects those gates.

## Finalization fault-injection matrix

Inject process termination immediately after each boundary; restart against the same isolated store and run recovery plus invariant checks.

| Boundary | Expected recovery and invariant |
|---|---|
| Completion intent temp write, before fsync/replace | Existing open upload remains open; no complete visibility |
| Intent replace/fsync, before WAV creation | Recovery sees validated intent and resumes or safely reopens; never permanently busy |
| WAV temp write in progress | Partial temp is ignored/cleaned or overwritten; source chunks remain intact |
| WAV file fsync, before WAV replace | Recovery validates temp and resumes replace or rewrites from chunks |
| WAV replace, before incoming directory fsync | Recovery validates canonical file/hash and continues; no premature complete visibility |
| Completed-manifest temp write/replace/fsync | Recovery chooses valid old intent or complete manifest atomically; no truncated JSON accepted |
| Complete marker write/fsync | Marker is published only after media and manifest validate |
| Incoming directory fsync, before directory rename | Valid incoming complete candidate is resumed; inbox remains absent until publication |
| Destination parent create/fsync and directory rename | Exactly one of incoming or canonical directory is authoritative; recovery removes no valid copy |
| Source/destination parent fsync after rename | Recovery discovers canonical directory and repairs index/inbox idempotently |
| First/each recipient inbox append, including broadcast | Missing rows are reconstructed once; existing rows are not duplicated |
| Chunk cleanup, including partial cleanup | Canonical media remains valid; leftover chunks are safe to remove on restart |
| Cull directory rename, deletion metadata write/fsync, audit append | Startup reconstructs complete trash state and audit/reconciliation; no message silently disappears |
| Restore rename and deletion metadata cleanup | Startup finds exactly one complete canonical or trash directory and can retry restore |

For each case assert filesystem topology, manifest parseability/state, media/source hashes when applicable, complete-marker visibility, inbox multiplicity per recipient, route visibility, and absence of unsafe path references. Record the injected boundary identifier in the JSON result.

## Evidence artifacts

Retain under `docs/evidence/h34-message-store/` (or a dated subdirectory beneath it):

- `README.md`: scope, prerequisites, exact commands, platform/filesystem, Python/dependency versions, known limitations, and links to all results.
- Qualification JSON: run ID, source revision, fixture parameters, counts, raw/WAV hashes, per-gate results, memory results, fault outcomes, and filesystem durability capabilities.
- Human-readable run log with the same run ID and PASS/FAIL per gate.
- A compact generated fixture manifest (generator version/seed and expected PCM/WAV sizes/hashes); no need to retain generated audio payload.
- Fault-injection matrix results and recovery diagnostics, including each crash boundary and the observed post-restart invariant result.
- Sanitized before/after directory inventory and inbox/trash inventory proving complete-only visibility and exact cull selection.

Do not include bearer tokens, private device configuration, audio payload, or unrelated H32 evidence.

## Commit boundary

Keep this plan as a standalone planning commit. Implementation should be split into reviewable commits with these boundaries:

1. **Authorization and protocol contract:** admin role checks, completed-message ownership/idempotency, normalized cull selection/token binding, strict protocol validation, focused tests.
2. **Durable state machine:** atomic/fsynced writes, restart reconciliation, idempotent inbox repair, crash-injection tests.
3. **Streaming and trash recovery:** bounded Range streaming, startup trash index, crash-safe cull/restore, route and hash tests.
4. **Qualification and status:** hermetic 180-second fixture/memory harness, evidence artifacts, and roadmap/demo status corrections.

Do not combine unrelated H32 hardware/firmware changes with any h34 implementation commit. Do not mark Phase 3 complete until all four gates pass and evidence is committed.

## Related documents

- [Message protocol](../MESSAGE-PROTOCOL.md)
- [Server message storage contract](../SERVER-MESSAGE-STORAGE.md)
- [Long-message experiments](long-message-experiments.md)
- [Current h34 demo plan](message-store-demo.md)
- [h34 evidence README](../evidence/h34-message-store/README.md)
