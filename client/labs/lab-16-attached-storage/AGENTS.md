# Agent and implementer guide — LAB-16 attached-storage memory campaign

**Scenario definitions, beats, measurements tables, and evidence status:** [`README.md`](README.md).

Use this file when implementing runners, probes, fixtures, verification, or delegating a single scenario to an agent.

## Host IP discovery and server ownership

Use the **host's IP address** as seen from the device. Do not put a workstation's private IP directly in firmware, a command example, or a fixture. LAB-16's build calls `client_address()` in [lab16_build.py](../../../scripts/lab16_build.py): by default it asks the operating system to choose the source IPv4 address for a UDP route lookup, then uses the selected address in the firmware overlay. Windows, macOS, and Linux each make that choice from their active routing table. The lookup selects a local address and sends no datagram. If the selected interface is not reachable from the kit (for example, a VPN or multi-NIC setup), set `LAB16_CLIENT_ADDRESS` to the reachable host IPv4 and verify connectivity before flashing. The `127.0.0.1` sdkconfig default is for local development; a physical-kit build must use its resolved host IP. Record the resolved host IP and whether discovery or the override supplied it in the run manifest.

Each LAB-16 `VARIANT=` run must own a **new server process for that scenario's run ID**, start it at the scenario-defined time, bind it for device access, write its log into that run's evidence bundle, and stop it during cleanup. Never reuse another scenario's server process, run ID, accepted-chunk state, or evidence directory. VARIANT `server-offline-during-record` owns its process too, but intentionally starts it only after recording finishes; VARIANT `live-stream-failover-during-record` changes its own fixture's acceptance state during its run.

The [V2 startup reference capture](../../../docs/labs/v2-comms-memory/README.md#reference-v2-startup) is not a LAB-16 run and cannot substitute for a same-boot LAB-16 baseline. Historical LAB-16 functional PASS results do not contain the memory samples requested here. Their PASS status remains useful for the functional behaviors they exercised, but all four variants must be run again after memory probes are added. V2 `CASE=` scenarios and their agent guide live under [docs/labs/v2-comms-memory/](../../../docs/labs/v2-comms-memory/README.md).

## Scenario, variant, and evidence contract

Use this block when implementing or executing any scenario in the campaign README.

Every scenario entry must identify its case/variant, the question it answers, exact input and fixture source, command, preconditions, ordered beats, observable pass/fail criteria, required measurements, and current evidence state. Keep separate behaviors in separate variants. If a linked product contract and a proposed lab behavior conflict, stop that case as OPEN and record the conflict instead of changing product behavior to satisfy the fixture.

1. **Keep one stable variant per scenario.** Use the registered LAB-16 `VARIANT` name. The command, firmware build/configuration, host fixture behavior, and manifest `capture.variant` must agree. Register runnable variants in `config/labs.registry.yaml`; never describe a proposed command as runnable before its target and fixture exist.
2. **Keep variant source material versioned with its implementation.** Firmware behavior and build configuration belong under `client/labs/<lab-slug>/`; host-server behavior and fixture generators belong under `server/labs/<fixture-slug>/`. Store each deterministic variant's fixture definition, generator or source assets, schema/version, seed, timing schedule, expected sizes, and checksums in a version-controlled `fixtures/<variant-id>/` directory beside the host fixture. Do not use a run's generated output as the only copy of its source fixture. LAB-16 firmware: `client/labs/lab-16-attached-storage/main/app_main.c` plus its SDK configuration; host controls: `scripts/lab_adapter_lab16.py` and `server/labs/lab-16-attached-storage/`.
3. **Make inputs reproducible and safe.** Use synthetic identities and fixture content. Do not commit credentials, household data, or raw microphone recordings. Record the fixture ID/revision, generator version, seed, and hash in the run manifest or a linked fixture manifest. Keep generated large or sensitive outputs out of the source fixture.
4. **Run one fresh boot per scenario on the exact scenario build.** Capture the clean baseline in that same boot before SD mount, microphone/I2S initialization, or Wi-Fi. Do not use the V2 product startup reference or a different run/build as a substitute. A separate baseline flash is not comparable evidence.
5. **Capture the same memory fields at each scenario's named beats.** Record allocator-managed internal heap total/free/largest block, DMA-capable internal heap total/free/largest block, and PSRAM heap total/free/largest block. Also record minimum-free watermarks for each region and the minimum sampled largest-block size, with monotonic time and beat/window ID. Sample at each beat and throughout long/high-load intervals so the interval low-water mark is preserved. Record raw byte counts; convert table values to KiB (1 KiB = 1,024 bytes). DMA-capable internal SRAM is a subset of internal heap and must not be added to it.
6. **Also capture the scenario's own functional data.** Include monotonic time and beat ID; operation state; request/response or PCM byte counts; queue and outstanding-buffer count/bytes/region; retry/failure/acceptance counts; and SD readback or server-side checks where relevant. For each app-owned buffer, record allocation/reallocation/free boundaries, requested bytes, capability/region, success or failure, and outstanding count/bytes while it is live. Do not infer concurrency or buffer lifetime from a queue size or a completion marker.
7. **Write one immutable evidence bundle per run, including incomplete runs.** Create `docs/labs/evidence/<evidence-slug>/runs/<run_id>/` with `run-manifest.json`, `events.jsonl`, `metrics.json` whenever any memory metrics were captured (including partial captures, with completeness/status recorded), and the host/server log and notes. Save raw serial at `logs/device/labs/<evidence-slug>/<run_id>/serial.txt`. The manifest must carry the exact UTC start/end bounds, run ID, selected kit, build/source identity, variant, fixture identity, resolved host IP and discovery/override method, and valid references/hashes for files that exist. Link the run ID/date in README.md to that manifest; link the raw artifacts directly as well. Publish accepted KiB summary tables under `docs/labs/evidence/lab-16/results/<variant>.md` and regenerate with `make lab.lab16.results` after captures change.
8. **Report separate statuses honestly.** `NOT RUN` means no scenario run exists. `NOT CAPTURED` means a run exists but the requested measurement/artifact is absent. `OPEN` means a fixture, runner, or acceptance detail is unresolved. A functional PASS without the requested memory data is not a memory PASS; mark the memory result unmeasured and schedule a new run. Never invent a date, measurement, build identity, or artifact link.

### Run-to-run variation defaults

Use these as initial repeatability tolerances; the upload variant has one accepted pair (`20261025-upload-manual-01` / `02`). Other variants have no memory pair yet. A scenario may set a **stricter** threshold before its first run when its contract or measurement needs require it. Do not widen a threshold after seeing results. These tolerances decide whether two captures are repeatable; they do not replace functional pass/fail limits or minimum-memory requirements.

- Compare corresponding per-beat and per-window readings from the same build, variant, fixture revision, and boot phase. For each pair of numeric readings, compute `abs(A - B)` and compare it to the larger of the absolute floor and the percentage of the pair mean.
- Internal and DMA-capable internal free/minimum-free readings and largest-block minima: `max(1 KiB, 5% of the pair mean)`. DMA-capable internal SRAM is a subset of internal SRAM; do not treat those two readings as independent evidence when deciding whether a third run is needed.
- PSRAM free/minimum-free readings and largest-block minima: `max(16 KiB, 0.5% of the pair mean)`.
- Per-task stack high-water, converted to bytes using the recorded SDK unit: `max(1 KiB, 5% of the pair mean)`.
- Operation durations and request latencies: `max(250 ms, 10% of the pair mean)`, unless a scenario contract or timeout requires a stricter threshold. A timing value outside its functional limit fails that run even if it is within the repeatability tolerance.
- Fixture-defined request/response bytes, PCM bytes, chunk counts and sequences, hashes, statuses, selected variant/build/fixture identity, and required concurrency facts must match exactly. For any other dynamic metric, state its tolerance in that scenario before the first run; if it has no tolerance, classify its repeatability as OPEN rather than inventing one after capture.
- Two runs agree only when every required numeric repeatability measurement is within its applicable tolerance and all exact fields match. Do not average incompatible runs or choose a different matching pair for each metric.
- A third run is permitted only when the first two runs are complete and independently verified, and at least **three required numeric measurements** exceed tolerance across at least **two measurement groups** (internal/DMA heap, PSRAM, task stack, buffer/queue occupancy, or operation timing). If either run is incomplete or the mismatch is smaller/narrower than this trigger, preserve the evidence and report repeatability as OPEN; this prompt does not authorize an extra run in those cases.

## How to read the memory numbers

The ESP32-S3 BOX-3 has **512 KiB of physical internal SRAM** and **16,384 KiB (16 MiB) of physical external PSRAM**. The measured heap totals are smaller than those physical capacities. Physical capacity, allocator-managed heap, allocated bytes, and free bytes are different numbers.

| Reading | What it measures | Region |
|---|---|---|
| Internal SRAM heap total/free/largest | Allocator-managed 8-bit internal heap, including its largest contiguous block | Internal SRAM |
| DMA-capable SRAM heap total/free/largest | Heap queried with MALLOC_CAP_DMA and MALLOC_CAP_INTERNAL, including its largest contiguous block | DMA-capable subset of internal SRAM; not PSRAM |
| PSRAM heap total/free/largest | Allocator-managed PSRAM heap, including its largest contiguous block | External PSRAM |
| Minimum-free watermark | Lowest allocator free reading since heap initialization or the explicitly opened local window | Each capability region |
| Sampled largest-block minimum | Lowest observed contiguous block during the interval | Each capability region |

DMA-capable SRAM is already included in the internal SRAM heap reading. **Do not add those two readings together.** The PSRAM measurement is separate. The largest DMA block is a contiguous-allocation limit; it is not the total free DMA-capable amount.

All tables use KiB, where 1 KiB = 1,024 bytes. Wire LAB-16 probes to the same ESP-IDF capability filters as the product helper in [product_mem_probe.c](../../../client/device/main/product_mem_probe.c) (field definitions for V2 runs: [v2-comms-memory AGENTS.md](../../../docs/labs/v2-comms-memory/AGENTS.md)). Published tables in `docs/labs/evidence/lab-16/results/` show the core readings for each capture; future `metrics.json` and event records must include the extended watermark, largest-block, stack, and app-owned buffer fields defined above.

The LAB-16 serial logs currently say that 16,384 KiB of PSRAM was added to the heap and that 32 KiB of internal memory was reserved for DMA/internal allocations. Those are heap setup and allocator-policy messages. **They are not free-memory measurements and do not mean only 32 KiB of DMA exists.** Existing LAB-16 run logs do not contain phase-tagged free-heap or largest-DMA readings.

Each scenario in README.md uses the same memory fields, with scenario-specific measurement moments. **NOT CAPTURED** means the linked run artifacts contain no value for that metric/moment; it does not mean zero.

## Test campaign procedure

For every run, first confirm the selected kit/port, build identity, variant, fixture revision, resolved host IP, and server route. Capture baseline and scenario beats on the same boot. Each VARIANT run starts its own LAB-16 diary server on port 8776 as described above.

### Spike and transient-peak capture

An event snapshot is useful for comparing named states, but it is not sufficient evidence for a spike that allocates and frees between two beats. Treat each scenario's capture window as the interval between its named beat boundaries, and report both the interval minimum and the event samples that explain it.

- Sample current heap totals/free and largest free blocks every **50 ms** during long or high-load windows. Use a preallocated monitor/aggregate; do not print a serial line on every tick. Keep the minimum and its monotonic timestamp for each memory region and largest-block reading. If a known buffer can have a shorter lifetime than the interval, bracket that allocation and release with event probes; the periodic sampler alone cannot guarantee it sees the transient.
- Record allocator minimum-free watermarks for internal, DMA-capable internal, and PSRAM at the same-boot baseline and each beat. These allocator watermarks are cumulative since heap initialization unless the selected SDK API explicitly provides a case-local window. If they cannot be reset, label them cumulative and derive the run's additional low-water delta against its baseline; do not label them interval-local. A periodic sample supplies interval-local timestamps for current free space and largest blocks.
- Probe before and after the known memory transitions: microphone/I2S open, SD writer setup, WAV/PCM or media-buffer allocation, HTTP response-body growth, parse/apply/render, queue handoff, retries, and buffer release. Include allocation failure and fallback-to-another-region outcomes. Whole-heap watermarks show aggregate spikes, including library/driver allocations; event probes attribute only the app-owned allocations they instrument.
- Capture stack high-water for every task participating in a variant, together with task name, configured stack size, and the unit returned by the selected SDK API.
- LAB-16 has no phase-tagged heap probe today. Reuse the product probe patterns in [product_mem_probe.c](../../../client/device/main/product_mem_probe.c) where practical, but LAB-16 still needs the 50 ms interval minima and beat-boundary summaries defined above.

### Scenario-by-scenario peak-beat audit

The variants already name the main functional phases. The additions below make their short-lived memory peaks observable; keep them inside the existing phases rather than treating each as a new functional variant.

| Variant | Peak-focused beat or clarification to capture |
|---|---|
| `upload` — record to SD, then upload | Add probes immediately before/after microphone/I2S open and at the first SD write. Keep a separate low-water window for recording/readback and for upload, including the live WAV/PCM allocation, HTTP result, and release. |
| `server-offline-during-record` | Capture the same microphone/SD allocation edges as `upload`. Timestamp host-side offline and online transitions against device events; keep the recording low-water window separate from the later first request/upload window. |
| `overlap-record-during-upload` | Mark overlap start only after session 1 is confirmed in flight. Capture the first and last message-2 SD writes while session 1 remains active, then the session-1 completion/release boundary; report the combined low-water for that proven overlap. |
| `live-stream-failover-during-record` | Bracket the long-lived `sec_pcm` allocation, each WAV request-buffer allocation/release and retry, and each drain PCM allocation/release. Preserve low-water and failure/recovery timing across the outage, restored acceptance, and drain windows. |

V2 `CASE=` peak-beat guidance: [v2-comms-memory AGENTS.md](../../../docs/labs/v2-comms-memory/AGENTS.md).

### Capture implementation effort

Capturing the required data is practical without global allocator interception. Adding LAB-16 phase probes aligned with [product_mem_probe.c](../../../client/device/main/product_mem_probe.c) is **low effort**. Wiring event probes around microphone/SD/upload boundaries is **moderate effort** because the lab firmware already has named allocation and lifecycle points. A low-overhead 50 ms sampler with interval minima, per-region watermark readings, and stack-watermark reporting is **moderate effort**; verify sampling overhead on hardware. Keep periodic sampling aggregated and flush summaries at beat boundaries so serial output does not dominate the capture. Attributing every internal ESP-IDF, HTTP, Wi-Fi, and driver allocation to a specific call would require heap tracing or allocator hooks and is **high effort**; it is not required for the first evidence pass.

## Single-scenario orchestrator prompt

Copy this prompt for one scenario at a time. Replace the bracketed fields before handing it to an implementation agent.

```text
You are the orchestrator for exactly one scenario in the device-memory lab campaign.

Scenario: [section number and title]
Variant/case ID: [registered VARIANT or proposed CASE]
Workspace: [repository/worktree]

README.md in this directory is the source of truth for the scenario, fixture, beats, measurements tables, and current evidence status. This AGENTS.md is the source of truth for tolerance rules, host/run procedure, evidence contract, and instrumentation expectations for LAB-16 VARIANTs. (V2 `CASE=` campaigns use [docs/labs/v2-comms-memory/](../../../docs/labs/v2-comms-memory/README.md) and its AGENTS.md.) Do not change neighboring scenarios or silently change product behavior to fit a fixture. You are authorized to delegate and complete the bounded phases below.

Delegate each phase to a suitable subagent: implementer, fresh read-only implementation verifier, scenario executor, fresh read-only results verifier for each run, and evidence/documentation recorder. Keep these responsibilities separate. A verifier must not verify its own work, and the executor must not verify its own run. The orchestrator owns the phase gates and applies the run limits below.

1. IMPLEMENT AND VERIFY
   - Have an implementation subagent implement this scenario only, including its declared variant/case, versioned source fixture, runner wiring, required beats/measurements, and evidence output.
   - Have a fresh read-only implementation verifier check the changes against README.md, AGENTS.md, linked contracts, fixture identity, build/command path, and required evidence. Include appropriate build and implementation checks.
   - You are authorized for up to three implement/verify cycles total, stopping as soon as verification passes. If verification still fails after cycle three, record the unresolved findings and stop before scenario execution.

2. EXECUTE AND VERIFY RESULTS
   - Before the first run, confirm preconditions, the exact build/configuration, variant and fixture revision/hash, the scenario's required beats, and applicable repeatability tolerances. Use the defaults in AGENTS.md; a scenario may be stricter only if that was declared before the first run. Do not invent or widen tolerances after seeing results.
   - Execute the scenario at least twice. Each execution gets a fresh boot, unique run ID, same verified build and fixture revision, complete required beat capture, and a new server process owned by that run when the scenario uses a host server. Resolve the endpoint using the host-IP procedure in AGENTS.md.
   - For each run, immediately preserve its immutable evidence bundle, including incomplete or failed runs. Then assign a fresh read-only results verifier to check the manifest bounds and identities, raw device/host evidence, required measurements and beats, acceptance criteria, and the stated outcome. Do not accept a completion marker alone as proof.
   - After the first two runs are complete and independently verified, compare every required measurement using AGENTS.md tolerances. A third scenario execution and independent result verification are authorized only if at least three required numeric measurements exceed tolerance across at least two measurement groups. Do not run a third merely to increase confidence or to replace an incomplete first/second run. Do not exceed three scenario executions under this authorization.
   - If either of the first two runs is incomplete, or the mismatch does not meet the third-run trigger, preserve the evidence and report the result as OPEN/INCOMPLETE with the reason. Ask for additional run authorization if another run is needed.

3. RECORD THE SCENARIO RESULT
   - Always keep each run's raw evidence linked, regardless of whether the runs agree.
   - Update the scenario's authoritative result summary in README.md and regenerate `docs/labs/evidence/lab-16/results/` only when at least two complete, independently verified runs agree across all required measurements: every numeric value is within its declared tolerance and all exact fields match. If a third run was authorized, a single pair must agree across the full measurement set; retain and report all three runs.
   - Record the accepted pair, per-run values, differences, tolerances, functional verdict, and links to each manifest, events, metrics, serial capture, and server log. Mark PASS only when both repeatability and the scenario's functional criteria pass. Record a stable failure as FAIL; otherwise keep OPEN/INCOMPLETE. Never overwrite or edit an immutable run bundle.

Finish with a concise report of implementation cycles, independent verification findings, run IDs and statuses, comparison/tolerance result, updated document location, and anything still OPEN. Stop at the authorization limits above.
```

## Implementation handoff order

1. **Repair LAB-16 evidence first.** Preserve multipart boundaries in the diary parser and verify exact received chunk sizes/content hashes. Add same-boot memory probes to all four existing scenario builds. Then rerun each LAB-16 VARIANT individually and link each new manifest, serial log, server log, per-beat events, and memory metrics in README.md.

V2 communication-load handoff steps: [v2-comms-memory AGENTS.md](../../../docs/labs/v2-comms-memory/AGENTS.md).

## LAB-16 implementation references

- [LAB-16 firmware variants](main/app_main.c)
- [LAB-16 WAV upload code](main/lab16_upload.c)
- [Product memory probe](../../../client/device/main/product_mem_probe.c) (capability filters to mirror in LAB-16 probes)
