# Agent and implementer guide — V2 communication-load memory campaign

**Scenario definitions, beats, measurements tables, and evidence status:** [`README.md`](README.md).

Use this file when implementing the proposed `make lab.v2-comms` runner, V2 fixtures, probes, verification, or delegating a single `CASE=` to an agent.

LAB-16 attached-storage `VARIANT=` scenarios: [lab-16 AGENTS.md](../../../client/labs/lab-16-attached-storage/AGENTS.md).

## Host IP discovery and server ownership

Use the **host's IP address** as seen from the device. Do not hard-code a workstation private IP in firmware, command examples, or fixtures. The V2 runner must resolve the reachable host IPv4 (same discovery pattern as LAB-16: route lookup with an optional override) and pass it as `SERVER=<HOST_IP>:8777` in examples and manifests.

Each `CASE=` run must own a **new V2 fixture server process** for that run ID, bind it for device access on port **8777**, write its log into that run's evidence bundle, and stop it during cleanup. Never substitute the LAB-16 diary server on port 8776.

The README **Reference** block (V2 startup capture) is not a runnable `CASE`. It is not a substitute for a same-boot baseline on a communication case. Rows marked **NOT RUN** have no scenario evidence until a run produces artifacts.

## Scenario, case, and evidence contract

Use this block when implementing or executing any `CASE=` in the campaign README.

Every scenario entry must identify its case, the question it answers, exact input and fixture source, command, preconditions, ordered beats, observable pass/fail criteria, required measurements, and current evidence state. If a linked product contract and a proposed lab behavior conflict, stop that case as OPEN and record the conflict instead of changing product behavior to satisfy the fixture.

1. **Keep one stable case per scenario.** Use the proposed `CASE` name. The command, product firmware build/configuration, host fixture behavior, and manifest case identity must agree. Do not describe `make lab.v2-comms` as runnable before its registry entry, runner, and fixture exist.
2. **Keep case source material versioned with its implementation.** Host fixture behavior and fixture generators belong under `server/labs/<fixture-slug>/` (to be created). Store each deterministic case's fixture definition, generator or source assets, schema/version, seed, timing schedule, expected sizes, and checksums in `fixtures/<case-id>/`. Do not use a run's generated output as the only copy of its source fixture.
3. **Make inputs reproducible and safe.** Use synthetic identities and fixture content. Record fixture ID/revision, generator version, seed, and hash in the run manifest.
4. **Run one fresh boot per scenario on the exact scenario build.** Capture the clean baseline in that same boot before SD mount, microphone/I2S initialization, display construction, or Wi-Fi, as applicable. Do not use a different run/build or the startup reference table as a substitute for that case's same-boot baseline.
5. **Capture the same memory fields at each scenario's named beats.** Record allocator-managed internal heap total/free/largest block, DMA-capable internal heap total/free/largest block, and PSRAM heap total/free/largest block, plus minimum-free watermarks and sampled largest-block minima with monotonic time and beat/window ID. DMA-capable internal SRAM is a subset of internal heap and must not be added to it.
6. **Also capture the scenario's own functional data.** Include request/response or media byte counts; queue and outstanding-buffer count/bytes/region; retry/failure counts; and app-owned buffer allocation boundaries where instrumented.
7. **Write one immutable evidence bundle per run** under `docs/labs/evidence/<evidence-slug>/runs/<run_id>/` with manifest, events, metrics when captured, and host log. Raw serial at `logs/device/labs/<evidence-slug>/<run_id>/serial.txt`.
8. **Report separate statuses honestly.** `NOT RUN`, `NOT CAPTURED`, `OPEN`, and functional PASS without memory data follow the same rules as LAB-16 (see [LAB-16 AGENTS.md](../../../client/labs/lab-16-attached-storage/AGENTS.md) § Scenario contract).

### Run-to-run variation defaults

Use the LAB-16 campaign defaults in [LAB-16 AGENTS.md § Run-to-run variation defaults](../../../client/labs/lab-16-attached-storage/AGENTS.md#run-to-run-variation-defaults) until paired V2 memory runs exist to calibrate them.

## How to read the memory numbers

The ESP32-S3 BOX-3 has **512 KiB of physical internal SRAM** and **16,384 KiB (16 MiB) of physical external PSRAM**. DMA-capable SRAM is already included in the internal SRAM heap reading. **Do not add those two readings together.**

All tables use KiB (1 KiB = 1,024 bytes). The product capture logs DMA totals/free with `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, general internal totals/free with `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, and PSRAM totals/free with `MALLOC_CAP_SPIRAM` in [product_mem_probe.c](../../../client/device/main/product_mem_probe.c).

## Test campaign procedure

For every run, confirm kit/port, build identity, `CASE`, fixture revision, resolved host IP, and `SERVER=<HOST_IP>:8777`. Capture baseline and case beats on the same boot. A prior startup diagnostic did not confirm a response from its auto-selected host address; a case is not ready until the fixture endpoint and route are verified.

### Spike and transient-peak capture

Follow the 50 ms interval minima, watermark, stack high-water, and event-probe rules in [LAB-16 AGENTS.md § Spike and transient-peak capture](../../../client/labs/lab-16-attached-storage/AGENTS.md#spike-and-transient-peak-capture), adapted for V2 workers and response bodies.

The current product probe is event-triggered. It reports current DMA/internal/PSRAM free values, largest DMA block, an internal minimum-free value, and the caller's stack high-water (`uxTaskGetStackHighWaterMark(NULL)` — not a census of all tasks). It does not yet report DMA or PSRAM minimum-free watermarks, largest internal/PSRAM blocks, per-case interval minima, or app-owned outstanding buffer peaks. Extend it (or add a case monitor) to meet the spike protocol.

### Scenario-by-scenario peak-beat audit

| `CASE` | Peak-focused beat or clarification to capture |
|---|---|
| `signin-idle-baseline` | Separate response-body receipt/growth, parse, apply, result consumption, and release. Track low-water across the full 60-second refresh interval, not just before and after it. |
| `single-audio-3s` | Record each Range response/body allocation and result-queue lifetime through prefix readiness and playback; capture the peak while the response and retained media buffer coexist, then after release. |
| `single-audio-sketch-3s` | Bracket sketch allocation/release and capture the exact interval when audio and sketch buffers coexist (or prove they do not). Preserve the worker's actual request ordering. |
| `long-range-audio-sketch` | Keep periodic interval minima active for all 180 seconds and annotate each Range/sketch response and buffer lifetime. Include the highest live-media-buffer count/bytes and final release. |
| `eight-member-inbox` | Separate response receipt, parse/apply, and card construction peaks; capture the maximum while all eight cards are resident and the settled state after results drain. |
| `inbox-64-mixed` | Capture transient response/parse/apply/render minima and the 64-card resident state; measure the selected-card media allocation separately from carousel navigation and post-playback release. |
| `inbox-64-all-sketch` | Capture descriptor parse/render with no sketch fetch, then the selected sketch's allocation/replay peak while all cards remain resident and the after-switch/release state. |
| `inbox-near-limit` | Sample response-body growth, not only the fully live body: report its maximum allocated body capacity/bytes and the peak when body and parser data coexist, then the freed state. |
| `inbox-response-boundary` | Capture exact-limit body growth and parse/apply, then the over-limit rejection while its response storage is still live and after it is released; retain the valid prior inbox throughout. |
| `inbox-revision-update` | Capture parse and revision-gate peaks for each response, including equal/stale discard without apply, then confirm queues/result buffers are released and the prior card state remains. |
| `playback-inbox-refresh` | Align memory windows to actual request start/end intervals. Capture media buffers while inbox/last-viewed results wait in queues, preserving serialization and each worker's outstanding bytes. |
| `send-audio-only` | Capture the recording buffer handoff, each audio-part request/body and response lifetime, complete response, and send-result drain; include the largest part/request peak. |
| `send-audio-sketch` | Add the same boundaries as `send-audio-only` plus the audio-to-sketch part transition and sketch-part peak while retained message data is live. |
| `worker-overlap` | Tie the combined heap and largest-block minimum to an interval where all three workers are proven in flight. Capture each worker's outstanding buffers and stack watermark there, then capture every result/buffer drain. |
| `hangout-roster-0-1-8` | Capture response/parse/apply/render peaks separately at empty, one, eight, and seven members, including the removal/release boundary; keep the no-inbox-traffic assertion. |

### Capture implementation effort

The proposed V2 runner and fixture framework must be implemented before cases can run. Adding missing snapshot fields to the existing product helper is **low effort**. Wiring event probes across login, inbox, and send workers is **moderate effort**. A 50 ms sampler with interval minima is **moderate effort**. Keep periodic sampling aggregated; the current helper logs on each event probe. Full heap tracing is **high effort** and not required for the first pass.

## Single-scenario orchestrator prompt

Copy this prompt for one `CASE=` at a time. Replace bracketed fields before handing it to an implementation agent.

```text
You are the orchestrator for exactly one CASE= in the V2 communication-load memory campaign.

Scenario: [CASE id and README section title]
Case ID: [proposed CASE]
Workspace: [repository/worktree]

docs/labs/v2-comms-memory/README.md is the source of truth for the scenario, fixture, beats, measurements tables, and current evidence status. docs/labs/v2-comms-memory/AGENTS.md is the source of truth for tolerance rules, host/run procedure, evidence contract, and instrumentation expectations. Do not change neighboring cases or silently change product behavior to fit a fixture.

Follow the three-phase implement/verify, execute/verify results, and record flow in LAB-16 AGENTS.md (same authorization limits), substituting this README and AGENTS path for scenario truth.
```

## Implementation handoff order

1. **Build the V2 measurement runner.** Register its lab identity/evidence slug and implement `make lab.v2-comms CASE=...`. It must collect the exact-build startup baseline and case beats on one boot, use the V2 routes, and emit the common memory fields plus request/buffer telemetry.
2. **Version the V2 fixture sources.** Add a deterministic fixture/generator directory per case. Resolve the 256,000-byte and exact/over-limit bodies using supported schema fields only. Implement the one-second part-delay profile for `CASE=worker-overlap` as a named fixture input.
3. **Execute each `CASE=` one at a time.** Preserve each run ID and exact manifest bounds. Record `NOT RUN`, `NOT CAPTURED`, `OPEN`, or `INCOMPLETE` until evidence supports a stronger claim; update README.md with artifact links after each capture.

LAB-16 multipart and VARIANT memory reruns: [LAB-16 AGENTS.md § Implementation handoff](../../../client/labs/lab-16-attached-storage/AGENTS.md#implementation-handoff-order).

## V2 contract and implementation references

- [Endpoint health, login, roster, and refresh](../../product/communication-contracts/endpoint-health-and-login.md)
- [Inbox, media, Range playback, and last-viewed](../../product/communication-contracts/inbox-and-media.md)
- [Outgoing message parts and flsk2 sketch encoding](../../product/communication-contracts/message-delivery.md)
- [V2 device build and diagnostic instructions](../../../client/device/README.md)
- [Product memory probe](../../../client/device/main/product_mem_probe.c)
- [Inbox HTTP worker](../../../client/device/main/product_inbox_http_worker.cpp)
