# LAB-03 independent read-only verification

Verifier: fresh delegated agent `/root/lab03_verifier`; no source edits, build, flash, hardware, or service mutations. Review completed 2026-10-17 (America/Chicago). Final firmware review: no remaining issue should block target evaluation; no performance pass established.

Findings corrected before the final target build:

- Page work captures session at queue acceptance, rejects stale queued work, and validates the publication session again at UI application.
- Allocation counters use atomics; a heap allocation-failure callback records runtime failures.
- Paused and terminal UI publications retry after lock contention.
- Codec mute state is independent of publication success, so resume unmutes even if a pause snapshot was deferred.
- Cancellation reduction excludes terminals preceding a later stop request.
- Offscreen controls retain active media identity; the harness focuses B before pausing and moving it offscreen.

Verifier checks: ten registry/native/parser checks passed in the first pass; four non-socket LAB-03 checks passed again after corrections. Offline checks matched all fixture byte counts, SHA-256 values, and reconstructed short/dense stroke timestamps. The fixture manifest hash is `e5a2bca61e22453f9cddea26fc2f0167a3de257510a45d780b7fc24c6282f82c`.

No tracked changes were found in LAB-02, product firmware, shared client code, or the v1 archive. Direct model reuse and adaptation of LAB-02's retained-card pattern were consistent with the requested foundation.

Parser review also required chronological focus → accepted B play → codec submission, accepted A/B/A triplets with terminals, and completed dense progress before claiming those paths. Root applied those parser corrections. Derived heap usage from regional minima is labeled a conservative upper bound; sampled peak usage is separate.

Remaining evidence limitations: native tests do not execute the complete FreeRTOS/UI concurrency path. Submitted PCM position, codec write and LVGL invalidation are proxies for DAC position, first audible sample and LCD effect. Actual underruns, acoustic/LCD synchronization, paint timing, CPU utilization and drift require target evidence. The final target report must state the observed coverage and missing evidence.

## Stage-driven v2 re-verification

Target evidence from the first wall-time harness showed missed fetch stages and superseded dense playback. Root replaced it with atomic worker-stage observations, a 25-second per-scenario deadline, a separate five-second terminal-drain deadline, and a 600-second loop paced by completed workers. The verifier caught and root corrected an initial timeout that would have included the sustained state.

The final read-only pass found no blocker to target05. EAGAIN retry retains received bytes in the same HTTP body within the existing cancellation and 15-second operation deadline. Worker phase reset and repeated operation identity reads prevent a stale settled observation from starting an extra sustained cycle. A fresh-ready host timestamp bounds server correlation; filter request start timestamps after that boundary. Actual trace and external recording still determine coverage and outcome.

## Fresh final evidence audit

A second fresh read-only agent (`/root/lab03_final_verifier`) reviewed final source and saved artifacts after the sustained run. It found no blocking runtime defect and retained performance **INCOMPLETE**.

It independently reproduced all run06 metrics, all recorded artifact sizes/hashes, 232 accepted operations with 232 terminals, 20 A/B/A cycles, the 600.089936-second run, all 337,615 per-stroke observations, and server correlation (391 post-ready requests, one excluded, no unmatched media requests). It matched manual/scripted build IDs and binary/config hashes, validated registry/help dispatch, and confirmed run08's captured/fresh-ready startup metadata and `not_configured` manual scenario stage. Scope boundaries remained unchanged.

The auditor identified one remaining fixture coverage gap: the common method calls for background-colored strokes, while fixed v1 contains red/green/blue only. The report records this as requiring a new fixture revision and affected reruns; existing comparison inputs were preserved. Audible/DAC/LCD evidence, underruns, paint timing and CPU utilization remain missing. The report records the provisional continuous-focus proxy p95 miss (114ms) and unavailable synchronized capture.
