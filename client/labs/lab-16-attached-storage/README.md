# Device memory test campaign: LAB-16 and V2 product scenarios

Last updated: 2026-10-25

This is the source of truth for the device-memory scenarios in this campaign, their scenario-specific beats, required variant inputs, and evidence. It covers two separate targets:

- **LAB-16** is the scripted attached-storage and microphone lab. Sections 1–4 use registered `make lab.lab16` variants; every scenario run owns a fresh LAB-16 diary-server process on port 8776.
- **V2 product scenarios** use the product firmware and V2 contracts. Sections 5–18 require the proposed `make lab.v2-comms` runner and a V2 fixture; that command is not implemented yet.

## Host IP discovery and server ownership

Use the **host's IP address** as seen from the device. Do not put a workstation's private IP directly in firmware, a command example, or a fixture. LAB-16's build calls `client_address()` in [lab16_build.py](../../../scripts/lab16_build.py): by default it asks the operating system to choose the source IPv4 address for a UDP route lookup, then uses the selected address in the firmware overlay. Windows, macOS, and Linux each make that choice from their active routing table. The lookup selects a local address and sends no datagram. If the selected interface is not reachable from the kit (for example, a VPN or multi-NIC setup), set `LAB16_CLIENT_ADDRESS` to the reachable host IPv4 and verify connectivity before flashing. The `127.0.0.1` sdkconfig default is for local development; a physical-kit build must use its resolved host IP. Record the resolved host IP and whether discovery or the override supplied it in the run manifest.

Each invocation for Sections 1–4 must own a **new server process for that scenario's run ID**, start it at the scenario-defined time, bind it for device access, write its log into that run's evidence bundle, and stop it during cleanup. Never reuse another scenario's server process, run ID, accepted-chunk state, or evidence directory. Section 2 owns its process too, but intentionally starts it only after recording finishes; Section 4 changes its own fixture's acceptance state during its run. The V2 runner must also resolve and use the host's IP address for its port-8777 endpoint; use `SERVER=<HOST_IP>:8777` in examples and manifests, not a machine-specific literal.

Section 0 is a captured V2 startup reference. It is not a LAB-16 run and is not a baseline for any other run. Historical LAB-16 functional PASS results do not contain the memory samples requested here. Their PASS status remains useful for the functional behaviors they exercised, but all four variants must be run again after memory probes are added. V2 scenario rows marked **NOT RUN** have no scenario evidence and must stay that way until a run produces its artifacts.

## Table of contents

- [Host IP discovery and server ownership](#host-ip-discovery-and-server-ownership)
- [Scenario, variant, and evidence contract](#scenario-variant-and-evidence-contract)
- [Run-to-run variation defaults](#run-to-run-variation-defaults)
- [How to read the memory numbers](#how-to-read-the-memory-numbers)
- [Spike and transient-peak capture](#spike-and-transient-peak-capture)
- [Scenario-by-scenario peak-beat audit](#scenario-by-scenario-peak-beat-audit)
- [Capture implementation effort](#capture-implementation-effort)
- [Single-scenario orchestrator prompt](#single-scenario-orchestrator-prompt)
- [Test campaign procedure](#test-campaign-procedure)
0. [Captured V2 startup reference (not a reusable baseline)](#section-0)
1. [LAB-16 upload: record to SD, then upload](#section-1)
2. [LAB-16 server offline during recording](#section-2)
3. [LAB-16 record message 2 while message 1 uploads](#section-3)
4. [LAB-16 live upload with outage, SD fallback, and drain](#section-4)
5. [V2 signed-in idle inbox baseline](#section-5)
6. [V2 single short audio-only inbox item](#section-6)
7. [V2 short audio-plus-sketch item](#section-7)
8. [V2 long multi-range audio-plus-sketch playback](#section-8)
9. [V2 eight-item inbox across an eight-person hangout](#section-9)
10. [V2 maximum 64-item mixed inbox](#section-10)
11. [V2 64-item all-sketch card stress](#section-11)
12. [V2 inbox response near the 262,144-byte ceiling](#section-12)
12a. [V2 exact inbox response limit boundary](#section-12-boundary)
13. [V2 inbox revision add, equal, and stale responses](#section-13)
14. [V2 playback, media ranges, last-viewed, and inbox refresh](#section-14)
15. [V2 outgoing audio-only message](#section-15)
16. [V2 outgoing audio-plus-sketch message](#section-16)
17. [V2 health, session refresh, inbox refresh, and send overlap](#section-17)
18. [V2 signed-out hangout roster sizes](#section-18)

## Scenario, variant, and evidence contract

Use this block when implementing or executing any scenario in this document.

Every scenario entry must identify its case/variant, the question it answers, exact input and fixture source, command, preconditions, ordered beats, observable pass/fail criteria, required measurements, and current evidence state. Keep separate behaviors in separate variants. If a linked product contract and a proposed lab behavior conflict, stop that case as OPEN and record the conflict instead of changing product behavior to satisfy the fixture.

1. **Keep one stable variant per scenario.** Use the `VARIANT` name registered for LAB-16 or the `CASE` name proposed for V2. The command, firmware build/configuration, host fixture behavior, and manifest `capture.variant` must agree. Register runnable LAB variants in `config/labs.registry.yaml`; never describe a proposed command as runnable before its target and fixture exist.
2. **Keep variant source material versioned with its implementation.** Firmware behavior and build configuration belong under `client/labs/<lab-slug>/`; host-server behavior and fixture generators belong under `server/labs/<fixture-slug>/`. Store each deterministic case's fixture definition, generator or source assets, schema/version, seed, timing schedule, expected sizes, and checksums in a version-controlled `fixtures/<variant-id>/` directory beside the host fixture. Do not use a run's generated output as the only copy of its source fixture. For LAB-16, the existing firmware path is `client/labs/lab-16-attached-storage/main/app_main.c` plus its SDK configuration, and the host controls are in `scripts/lab_adapter_lab16.py` and `server/labs/lab-16-attached-storage/`. The V2 fixture directory and runner still need to be created.
3. **Make inputs reproducible and safe.** Use synthetic identities and fixture content. Do not commit credentials, household data, or raw microphone recordings. Record the fixture ID/revision, generator version, seed, and hash in the run manifest or a linked fixture manifest. Keep generated large or sensitive outputs out of the source fixture.
4. **Run one fresh boot per scenario on the exact scenario build.** Capture the clean baseline in that same boot before SD mount, microphone/I2S initialization, display construction, or Wi-Fi, as applicable. Do not use Section 0 or a different run/build as a substitute. A separate baseline flash is not comparable evidence.
5. **Capture the same memory fields at each scenario's named beats.** Record allocator-managed internal heap total/free/largest block, DMA-capable internal heap total/free/largest block, and PSRAM heap total/free/largest block. Also record minimum-free watermarks for each region and the minimum sampled largest-block size, with monotonic time and beat/window ID. Sample at each beat and throughout long/high-load intervals so the interval low-water mark is preserved. Record raw byte counts; convert table values to KiB (1 KiB = 1,024 bytes). DMA-capable internal SRAM is a subset of internal heap and must not be added to it.
6. **Also capture the scenario's own functional data.** Include monotonic time and beat ID; operation state; request/response or PCM byte counts; queue and outstanding-buffer count/bytes/region; retry/failure/acceptance counts; and SD readback or server-side checks where relevant. For each app-owned buffer, record allocation/reallocation/free boundaries, requested bytes, capability/region, success or failure, and outstanding count/bytes while it is live. Do not infer concurrency or buffer lifetime from a queue size or a completion marker.
7. **Write one immutable evidence bundle per run, including incomplete runs.** Create `docs/labs/evidence/<evidence-slug>/runs/<run_id>/` with `run-manifest.json`, `events.jsonl`, `metrics.json` whenever any memory metrics were captured (including partial captures, with completeness/status recorded), and the host/server log and notes. Save raw serial at `logs/device/labs/<evidence-slug>/<run_id>/serial.txt`. The manifest must carry the exact UTC start/end bounds, run ID, selected kit, build/source identity, variant, fixture identity, resolved host IP and discovery/override method, and valid references/hashes for files that exist. Link the run ID/date in this document to that manifest; link the raw artifacts directly as well.
8. **Report separate statuses honestly.** `NOT RUN` means no scenario run exists. `NOT CAPTURED` means a run exists but the requested measurement/artifact is absent. `OPEN` means a fixture, runner, or acceptance detail is unresolved. A functional PASS without the requested memory data is not a memory PASS; mark the memory result unmeasured and schedule a new run. Never invent a date, measurement, build identity, or artifact link.

### Run-to-run variation defaults

Use these as initial repeatability tolerances; no paired LAB-16 memory runs exist yet to calibrate them empirically. A scenario may set a **stricter** threshold before its first run when its contract or measurement needs require it. Do not widen a threshold after seeing results. These tolerances decide whether two captures are repeatable; they do not replace functional pass/fail limits or minimum-memory requirements.

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

All tables use KiB, where 1 KiB = 1,024 bytes. The source probe uses the ESP-IDF capability filters shown in [product_mem_probe.c](../../device/main/product_mem_probe.c). The V2 capture logs DMA totals/free with MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL, general internal totals/free with MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT, and PSRAM totals/free with MALLOC_CAP_SPIRAM. The historical tables below show the core readings available for each capture; future `metrics.json` and event records must include the extended watermark, largest-block, stack, and app-owned buffer fields defined above.

The LAB-16 serial logs currently say that 16,384 KiB of PSRAM was added to the heap and that 32 KiB of internal memory was reserved for DMA/internal allocations. Those are heap setup and allocator-policy messages. **They are not free-memory measurements and do not mean only 32 KiB of DMA exists.** Existing LAB-16 run logs do not contain phase-tagged free-heap or largest-DMA readings.

Each scenario below has the same memory fields, with scenario-specific measurement moments. **NOT CAPTURED** means the linked run artifacts contain no value for that metric/moment; it does not mean zero. The old LAB-16 runs predate the V2 clean-state capture, so the V2 reading in Section 0 cannot be passed off as their same-run baseline.

## Test campaign procedure

For every run, first confirm the selected kit/port, build identity, variant, fixture revision, resolved host IP, and server route. Capture baseline and scenario beats on the same boot. Each Sections 1–4 run starts its own LAB-16 diary server as described above. V2 cases use the proposed runner and the V2 contract fixture on port 8777; never substitute the LAB-16 diary server. A prior V2 startup diagnostic did not confirm a response from its auto-selected host address, so a V2 case is not ready until the fixture endpoint and route are verified.

### Spike and transient-peak capture

An event snapshot is useful for comparing named states, but it is not sufficient evidence for a spike that allocates and frees between two beats. Treat each scenario's capture window as the interval between its named beat boundaries, and report both the interval minimum and the event samples that explain it.

- Sample current heap totals/free and largest free blocks every **50 ms** during long or high-load windows. Use a preallocated monitor/aggregate; do not print a serial line on every tick. Keep the minimum and its monotonic timestamp for each memory region and largest-block reading. If a known buffer can have a shorter lifetime than the interval, bracket that allocation and release with event probes; the periodic sampler alone cannot guarantee it sees the transient.
- Record allocator minimum-free watermarks for internal, DMA-capable internal, and PSRAM at the same-boot baseline and each beat. These allocator watermarks are cumulative since heap initialization unless the selected SDK API explicitly provides a case-local window. If they cannot be reset, label them cumulative and derive the run's additional low-water delta against its baseline; do not label them interval-local. A periodic sample supplies interval-local timestamps for current free space and largest blocks.
- Probe before and after the known memory transitions: microphone/I2S open, SD writer setup, WAV/PCM or media-buffer allocation, HTTP response-body growth, parse/apply/render, queue handoff, retries, and buffer release. Include allocation failure and fallback-to-another-region outcomes. Whole-heap watermarks show aggregate spikes, including library/driver allocations; event probes attribute only the app-owned allocations they instrument.
- Capture stack high-water for every task participating in a case, together with task name, configured stack size, and the unit returned by the selected SDK API. The existing V2 probe calls `uxTaskGetStackHighWaterMark(NULL)`, so its `stack_hw` is for the task making that particular probe call; it is not a census of all task stacks.
- The current V2 probe is event-triggered, not periodic. It reports current DMA/internal/PSRAM free values, largest DMA block, an internal minimum-free value, and the caller's stack high-water. It does not currently report DMA or PSRAM minimum-free watermarks, largest internal/PSRAM blocks, per-case interval minima, or app-owned outstanding buffer peaks. LAB-16 has no phase-tagged heap probe today. The existing probe is a useful base, but neither target currently meets the spike protocol above.

### Scenario-by-scenario peak-beat audit

The scenarios already name the main functional phases. The additions below make their short-lived memory peaks observable; keep them inside the existing phases rather than treating each as a new functional case. Section 0 is a historical startup reference and is not a substitute for any of these beats.

| Scenario | Peak-focused beat or clarification to capture |
|---|---|
| 1 — LAB-16 record to SD, then upload | Add probes immediately before/after microphone/I2S open and at the first SD write. Keep a separate low-water window for recording/readback and for upload, including the live WAV/PCM allocation, HTTP result, and release. |
| 2 — LAB-16 server offline during recording | Capture the same microphone/SD allocation edges as Scenario 1. Timestamp host-side offline and online transitions against device events; keep the recording low-water window separate from the later first request/upload window. |
| 3 — LAB-16 record message 2 during message 1 upload | Mark overlap start only after session 1 is confirmed in flight. Capture the first and last message-2 SD writes while session 1 remains active, then the session-1 completion/release boundary; report the combined low-water for that proven overlap. |
| 4 — LAB-16 live outage, fallback, and drain | Bracket the long-lived `sec_pcm` allocation, each WAV request-buffer allocation/release and retry, and each drain PCM allocation/release. Preserve low-water and failure/recovery timing across the outage, restored acceptance, and drain windows. |
| 5 — V2 signed-in idle inbox | Separate response-body receipt/growth, parse, apply, result consumption, and release. Track low-water across the full 60-second refresh interval, not just before and after it. |
| 6 — V2 short audio-only item | Record each Range response/body allocation and result-queue lifetime through prefix readiness and playback; capture the peak while the response and retained media buffer coexist, then after release. |
| 7 — V2 short audio-plus-sketch item | Bracket sketch allocation/release and capture the exact interval when audio and sketch buffers coexist (or prove they do not). Preserve the worker's actual request ordering. |
| 8 — V2 long multi-range playback | Keep periodic interval minima active for all 180 seconds and annotate each Range/sketch response and buffer lifetime. Include the highest live-media-buffer count/bytes and final release. |
| 9 — V2 eight-item inbox | Separate response receipt, parse/apply, and card construction peaks; capture the maximum while all eight cards are resident and the settled state after results drain. |
| 10 — V2 maximum 64-item mixed inbox | Capture transient response/parse/apply/render minima and the 64-card resident state; measure the selected-card media allocation separately from carousel navigation and post-playback release. |
| 11 — V2 64-item sketch-card stress | Capture descriptor parse/render with no sketch fetch, then the selected sketch's allocation/replay peak while all cards remain resident and the after-switch/release state. |
| 12 — V2 response near 262,144 bytes | Sample response-body growth, not only the fully live body: report its maximum allocated body capacity/bytes and the peak when body and parser data coexist, then the freed state. |
| 12a — V2 exact limit boundary | Capture exact-limit body growth and parse/apply, then the over-limit rejection while its response storage is still live and after it is released; retain the valid prior inbox throughout. |
| 13 — V2 add/equal/stale revisions | Capture parse and revision-gate peaks for each response, including equal/stale discard without apply, then confirm queues/result buffers are released and the prior card state remains. |
| 14 — V2 playback plus refresh and last-viewed | Align memory windows to actual request start/end intervals. Capture media buffers while inbox/last-viewed results wait in queues, preserving serialization and each worker's outstanding bytes. |
| 15 — V2 outgoing audio-only message | Capture the recording buffer handoff, each audio-part request/body and response lifetime, complete response, and send-result drain; include the largest part/request peak. |
| 16 — V2 outgoing audio-plus-sketch message | Add the same boundaries as Scenario 15 plus the audio-to-sketch part transition and sketch-part peak while retained message data is live. |
| 17 — V2 health/session/inbox/send overlap | Tie the combined heap and largest-block minimum to an interval where all three workers are proven in flight. Capture each worker's outstanding buffers and stack watermark there, then capture every result/buffer drain. |
| 18 — V2 signed-out roster sizes | Capture response/parse/apply/render peaks separately at empty, one, eight, and seven members, including the removal/release boundary; keep the no-inbox-traffic assertion. |

### Capture implementation effort

Capturing the required data is practical without global allocator interception. These effort levels cover measurement instrumentation; the proposed V2 runner and fixture framework must still be implemented before V2 scenarios can run. Adding the missing snapshot fields to the existing V2 helper and adding equivalent LAB-16 probes is **low effort**. Wiring event probes around the known buffer, worker, queue, and state transitions is **moderate effort** because the code already has named allocation and lifecycle points, but V2 work crosses multiple asynchronous workers. A low-overhead 50 ms sampler with interval minima, per-region watermark readings, and stack-watermark reporting is **moderate effort**; a timer/monitor and its aggregation state still consume time and some resources, so verify the sampling overhead. The current V2 helper emits a log line at each event probe; keep periodic sampling aggregated and flush summaries at beat boundaries so serial output does not dominate the capture. Attributing every internal ESP-IDF, HTTP, Wi-Fi, and driver allocation to a specific call would require heap tracing or allocator hooks and is **high effort**; it is not required for the first evidence pass. Report unattributed runtime spikes honestly while still preserving the whole-heap low-water evidence.

<a id="section-0"></a>
## Section 0 — Captured V2 startup reference (not a reusable scenario baseline)

**Capture:** The first sample is taken before Wi-Fi startup, SD mounting, microphone initialization, or display/UI construction. The same diagnostic boot then records how heap readings change as storage, microphone, display, workers, and Wi-Fi are brought up. This reference is not a scenario run and cannot replace a same-boot baseline in Sections 1–18.

**Command used for the captured V2 diagnostic:**

    $env:FAMILY_DEVICE_MEM_DIAGNOSTIC = '1'
    make app KIT=lynn-kit PORT=COM5
    make app:monitor FILTER='product_network:I product_mic:I product_record:I product_mem:I product_send_http:I product_login:I product_login_http:I'

**Capture time and identity:** 2026-10-25, approximately 02:06:15 America/Chicago, from the host timestamp on [runtime-memory.txt](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt). The serial itself has boot-relative time (first full sample at t=19 ms), and this capture has no run manifest or run ID, so the host file time is approximate. Board: lynn-kit on COM5. Build: `80a3f355d76a40e88450691a3577cbb4`. The diagnostic boot markers passed.

**Detailed description:** In app startup, the first full memory sample is logged immediately before the SD mount call. At that point Wi-Fi has not started, the SD card is not mounted, the microphone/I2S has not initialized, and display/UI construction has not started. The later rows are from that same boot as each subsystem starts. This is a real V2 product capture, not a LAB-16 firmware baseline.

**Capture evidence:** The [V2 device memory snapshot](../../../docs/product/device-memory-snapshot-2026-10-25.md) explains the capture. Raw samples: [memory-samples.txt](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/memory-samples.txt). Filtered serial: [runtime-memory.txt](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt). Source call order: [app_main.c](../../device/main/app_main.c). This is a startup reference only; it did not run a V2 communication case or LAB-16 variant.

**Important beats:**

- Boot and capture the no-Wi-Fi, SD-unmounted, microphone-uninitialized baseline.
- Mount the SD card and take the next sample.
- Initialize the microphone/I2S and take the next sample.
- Build the display and initial UI, then start the send worker.
- Start Wi-Fi and wait for IP.
- Start the login HTTP worker and take the final boot sample.
- This run did not start recording and did not obtain a successful server response.

**Measurements (KiB):** Hardware capacities are 512 KiB internal SRAM and 16,384 KiB PSRAM. Heap totals/free are allocator readings, not the whole physical chip capacities.

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---:|---:|---:|---:|
| Before SD mount, microphone, display, and Wi-Fi | 245.79 / 145.27 | 237.84 / 137.69 | 62.00 | 16,290.16 / 16,287.67 |
| After SD mount | 245.79 / 144.28 | 237.84 / 136.70 | 62.00 | 16,290.16 / 16,259.10 |
| After microphone/I2S initialization | 245.79 / 137.39 | 237.84 / 129.81 | 62.00 | 16,290.16 / 16,255.97 |
| UI ready | 245.79 / 109.65 | 237.84 / 102.07 | 62.00 | 16,290.16 / 16,255.40 |
| Send HTTP worker ready | 245.79 / 104.48 | 237.84 / 96.91 | 62.00 | 16,290.16 / 16,239.40 |
| Wi-Fi received IP | 245.79 / 35.63 | 237.84 / 28.05 | 27.00 | 16,290.16 / 16,175.49 |
| Login HTTP worker ready | 245.79 / 26.34 | 237.84 / 18.76 | 18.00 | 16,290.16 / 16,159.48 |

At the clean first sample, the 512 KiB physical internal SRAM has 245.79 KiB in the queried internal heap and 145.27 KiB free in that heap. The remaining 266.21 KiB is outside that allocator query; it is not established as free app memory. Of the queried free internal heap, the DMA-qualified query reports 137.69 KiB free, including a largest contiguous block of 62 KiB. The internal-free minus DMA-free query difference is 7.58 KiB not included by the DMA filter. PSRAM reports 16,290.16 KiB in its heap and 16,287.67 KiB free. None of these values is LVGL pool size.

**LAB-16-specific baseline still needed:** The V2 measurement above cannot be used as the numerical baseline for LAB-16 firmware. Add the same capability probe to all four LAB-16 scenario builds and emit the initial sample after `LAB16_BOOT status=stable` but before `sd_session_open`, microphone/I2S initialization, or Wi-Fi. A standalone `memory-baseline` build may be used to check the probe, but its numbers cannot substitute for each scenario's same-boot baseline. Until the hook is present in a scenario build and its run captures the sample, mark that baseline NOT CAPTURED.

<a id="section-1"></a>
## Section 1 — LAB-16 upload: record to SD, then upload

**Scenario:** The device records microphone PCM to the attached SD card first. After the recording is complete and validated, it joins Wi-Fi and uploads the stored recording as one-second WAV chunks.

**Command:**

    make lab.lab16 KIT=lynn-kit PORT=COM5

This is the existing default upload variant.

**Historical functional run:** PASS. UTC bounds: [2026-10-23 05:41:36.831692–05:45:59.313600](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/run-manifest.json). Run ID: [20261023-upload-run-b193b616](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/run-manifest.json). A later interrupted attempt, [20261024-upload-run-c4c3662e](../../../docs/labs/evidence/lab-16/runs/20261024-upload-run-c4c3662e/run-manifest.json), ended UNMEASURED and does not replace the PASS run.

**Detailed description:** After a 500 ms post-flash settle, the firmware probes the SENSOR microSD with an LBA0 read, records 210 seconds of 16 kHz mono signed 16-bit PCM starting at LBA 2048, and validates the stored header plus first/middle/last samples. The capture is 6,720,000 PCM bytes (6,562.5 KiB), or 32,000 PCM bytes per second. It then connects to the LAB-16 diary server at port 8776 and uploads 210 one-second WAV requests. Each request contains 32,000 PCM bytes plus a 44-byte WAV header, 32,044 bytes total. This uses the LAB-16 v1 diary protocol, not V2 message routes.

Sources: [LAB-16 firmware flow](main/app_main.c), [WAV/chunk uploader](main/lab16_upload.c), [LAB-16 diary server](../../../server/labs/lab-16-attached-storage/server.py).

**Important beats:**

- Boot becomes stable after the 500 ms settle; take the clean memory baseline before the SD mount, microphone, and Wi-Fi work.
- Mount/probe the SENSOR card; record capacity, bus width/frequency, and the successful LBA0 read.
- Open the microphone and write the header at LBA 2048; sample during capture, including an interval low-water mark while SD writes are active.
- Finish 210 seconds / 6,720,000 PCM bytes; record header, first/middle/last readback, signal metrics, and `silent=0` validation.
- Join Wi-Fi; capture the first WAV request and sustained upload high-water while the 210 one-second posts run.
- Confirm 210 accepted chunks, zero failures, complete server count, and released/quiescent workers; capture recovery memory.

**Run evidence:** [manifest](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/run-manifest.json) · [events](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/events.jsonl) · [serial](../../../logs/device/labs/lab-16/20261023-upload-run-b193b616/serial.txt) · [server log](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/server.log).

**Memory evidence:** NOT CAPTURED in this historical functional run. Rerun `upload` after adding same-boot baseline and phase/low-water probes.

**Measurements (KiB):** The original run has no free-heap or DMA samples. The boot log's 16,384 KiB PSRAM pool and 32 KiB internal reserve are setup/configuration messages, not measurements.

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Baseline before SD, microphone, and Wi-Fi | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| SD + microphone recording active | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Wi-Fi and WAV upload active | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Upload complete, workers quiescent | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
<a id="section-2"></a>
## Section 2 — LAB-16 server offline during recording

**Scenario:** The device records the full message to SD while the server is not accepting connections. The server comes up only after recording has finished; the device then uploads the stored audio.

**Command:**

    make lab.lab16 VARIANT=server-offline-during-record KIT=lynn-kit PORT=COM5

**Historical functional run:** PASS. UTC bounds: [2026-10-23 05:46:06.603422–05:50:25.274147](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/run-manifest.json). Run ID: [20261023-server-offline-during-record-run-d9566b03](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/run-manifest.json).

**Detailed description:** This is the same 210-second, 16 kHz mono, signed 16-bit capture as Section 1: 6,720,000 PCM bytes to SD beginning at LBA 2048, followed by audio validation and 210 one-second WAV posts. The host delays starting the diary server until the firmware reports the SD recording complete. It checks that storage capture does not depend on server availability during the recording phase. It is not a V2 server test.

Sources: [variant control and device path](main/app_main.c), [LAB-16 diary server](../../../server/labs/lab-16-attached-storage/server.py).

**Important beats:**

- Host fixture is not accepting requests before boot; take the same-boot clean memory baseline, mount/probe the card, and record the LBA0 result.
- Open the microphone and record 210 seconds to SD while the diary server is stopped; sample during capture and at the SD validation peak.
- Finish 6,720,000 PCM bytes and validate header plus first/middle/last samples before any Wi-Fi join or upload begins.
- Host starts the diary server only after the record-complete marker; record that event and the subsequent Wi-Fi/IP state.
- Capture the first post-record request and sustained 210-chunk upload, then confirm 210 accepted / 0 failed and server-side count.
- Confirm worker cleanup and capture recovered memory.

**Run evidence:** [manifest](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/run-manifest.json) · [events](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/events.jsonl) · [serial](../../../logs/device/labs/lab-16/20261023-server-offline-during-record-run-d9566b03/serial.txt) · [server log](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/server.log).

**Memory evidence:** NOT CAPTURED in this historical functional run. Rerun `server-offline-during-record` after adding same-boot baseline and phase/low-water probes.

**Measurements (KiB):** This historical run did not log heap totals/free, DMA-capable free, or largest DMA block.

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Baseline before SD, microphone, and Wi-Fi | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Recording active with server offline | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Server starts and upload is active | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Upload complete, workers quiescent | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |

<a id="section-3"></a>
## Section 3 — LAB-16 record message 2 while message 1 uploads

**Scenario:** The device finishes one 180-second message and starts uploading it. While that upload runs, it records a second 180-second message to SD.

**Command:**

    make lab.lab16 VARIANT=overlap-record-during-upload KIT=lynn-kit PORT=COM5

**Historical functional run:** PASS. UTC bounds: [2026-10-23 06:15:00.446797–06:22:02.027734](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/run-manifest.json). Run ID: [20261023-overlap-record-during-upload-run-8d644d2f](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/run-manifest.json).

**Detailed description:** Each message is 180 seconds of 16 kHz mono signed 16-bit PCM: 5,760,000 bytes (5,625 KiB) per message, 11,520,000 PCM bytes across both. Message 1 is recorded to SD and uploaded as 180 one-second WAV chunks while message 2 is being recorded to a different SD LBA. Message 2 is then read back and uploaded as a second 180-chunk session. This is the LAB-16 variant closest to simultaneous old-message upload and new-message recording, but it does not contain the V2 UI or V2 inbox traffic.

Sources: [overlap implementation](main/app_main.c), [chunk uploader](main/lab16_upload.c).

**Important beats:**

- Cold boot and same-build baseline; mount/probe the card and initialize the capture path.
- Record message 1 for 180 seconds at LBA 2048; validate 5,760,000 PCM bytes and readback before upload.
- Join Wi-Fi, start upload session 1, and capture the upload worker's start plus first accepted chunk.
- During the measured overlap window, record message 2 for 180 seconds at the next aligned LBA while session 1 sends; sample both low-water memory and SD mutex/writer state.
- Validate message 2, wait for session 1's terminal result, then upload message 2 as session 2.
- Confirm 180/180 server chunks for each session, no missing sequence numbers, and worker/mutex/event cleanup; capture settled memory.

**Run evidence:** [manifest](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/run-manifest.json) · [events](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/events.jsonl) · [serial](../../../logs/device/labs/lab-16/20261023-overlap-record-during-upload-run-8d644d2f/serial.txt) · [server log](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/server.log).

**Memory evidence:** NOT CAPTURED in this historical functional run. Rerun `overlap-record-during-upload` after adding same-boot baseline and phase/low-water probes.

**Measurements (KiB):** This historical run did not log heap-capability readings.

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Baseline before SD, microphone, and Wi-Fi | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Message 1 recording | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Message 1 uploading while message 2 records (overlap peak) | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Message 2 uploading | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Both sessions complete and workers quiescent | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |

<a id="section-4"></a>
## Section 4 — LAB-16 live upload with outage, SD fallback, and drain

**Scenario:** The device posts each one-second audio chunk while recording. During a server outage, failed chunks are retained on SD; after recovery, the device drains them to the server.

**Command:**

    make lab.lab16 VARIANT=live-stream-failover-during-record KIT=lynn-kit PORT=COM5

**Historical functional run:** PASS. UTC bounds: [2026-10-23 10:40:27.435518–10:44:23.807147](../../../docs/labs/evidence/lab-16/runs/20261023-live-stream-failover-during-record-run-77755e4a/run-manifest.json). Run ID: [20261023-live-stream-failover-during-record-run-77755e4a](../../../docs/labs/evidence/lab-16/runs/20261023-live-stream-failover-during-record-run-77755e4a/run-manifest.json).

**Detailed description:** The take is 180 seconds of 16 kHz mono signed 16-bit PCM, totaling 5,760,000 bytes. Each one-second request carries 32,000 PCM bytes. The host schedules `accepting=false` 30 seconds after the streaming marker and restores acceptance 75 seconds after that marker. In the passing run, the serial recorded HTTP 503 failover for seq 30–60 inclusive (31 chunks), then recovered/drained those 31 chunks; 149 chunks uploaded live and all 180 were present at the server after recovery. These are observed run values, not a guarantee that future runs fail the same sequence numbers. In this implementation each loop reads audio, writes it to SD, and performs the HTTP post before reading the next second. This tests retry/failover and storage recovery; it does not prove that an independent microphone task captures while an HTTP call blocks.

Sources: [live failover device path](main/app_main.c), [LAB-16 diary protocol and server](../../../server/labs/lab-16-attached-storage/server.py).

**Important beats:**

- Cold boot; take the baseline, mount/probe the card, join Wi-Fi, initialize/open the microphone, and allocate the one-second PSRAM chunk buffer. Capture each setup beat separately.
- Begin the 180-chunk stream; at each chunk read, SD write, and HTTP result, preserve the actual sequence, response, and memory low-water.
- Record the host's acceptance-off event and every failed sequence retained for SD fallback; do not assume fixed sequence numbers from elapsed time.
- Record acceptance restoration, recovery uploads, and drain high-water while the microphone path is quiescent.
- Validate the full on-card PCM/header and final 180/180 server sequence set; record live-uploaded, drained, failed, and duplicate counts.
- Wait for buffers/workers to be released and capture post-drain recovery memory.

**Run evidence:** [manifest](../../../docs/labs/evidence/lab-16/runs/20261023-live-stream-failover-during-record-run-77755e4a/run-manifest.json) · [events](../../../docs/labs/evidence/lab-16/runs/20261023-live-stream-failover-during-record-run-77755e4a/events.jsonl) · [serial](../../../logs/device/labs/lab-16/20261023-live-stream-failover-during-record-run-77755e4a/serial.txt) · [server log](../../../docs/labs/evidence/lab-16/runs/20261023-live-stream-failover-during-record-run-77755e4a/server.log).

**Memory evidence:** NOT CAPTURED in this historical functional run. Rerun `live-stream-failover-during-record` after adding same-boot baseline and phase/low-water probes.

**Historical evidence qualification:** The four linked manifests preserve exact UTC run bounds and PASS verdicts, but their firmware/fixture identity and serial/server artifact fields are null. Their event journals contain only the terminal event, so phase timing must be read from the direct serial/server files. Those files exist and support the functional outcomes; these runs are not provenance-complete memory evidence. New runs must populate the build/source, SDK configuration, fixture revision/hash, per-beat events, metrics, and artifact references in the manifest.

**Open server-payload integrity issue:** The intended WAV request body is 32,044 bytes, but historical `server.log` files include some stored chunk lengths of 32,043 bytes. The current [multipart parser](../../../server/labs/lab-16-attached-storage/server.py) strips trailing CR/LF bytes from the binary payload; those bytes can be part of PCM data. The old 210/180 chunk counts prove sequence receipt, not exact payload integrity. Before a new run claims payload integrity, preserve the multipart boundary separately, record exact received body lengths, and compare per-chunk bytes or hashes with the source/readback data. Keep this OPEN until the fixture and verifier prove it.

**Measurements (KiB):** This historical run did not log heap-capability readings.

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Baseline before SD, microphone, and Wi-Fi | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Live recording and posting | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| HTTP 503 / SD fallback active | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| Server restored / drain active | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |
| 180 chunks accepted and workers quiescent | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED | NOT CAPTURED |

## V2 product communication-load test cases

These cases use the V2 product firmware and the V2 communication contracts; they are not LAB-16 diary-server runs. The signed-in product uses three serial HTTP workers: login/health/roster/refresh, inbox/media/last-viewed, and outgoing send. At most one request per worker should be in flight, so up to three exchanges can overlap across workers. Results can remain queued after a network exchange finishes; measure actual response-buffer count/bytes/region rather than inferring memory from queue depth.

The V2 inbox worker has a four-entry job/result queue, a 262,144-byte snapshot response cap, and a 16,384-byte cap for each media response slice. Response storage is requested from PSRAM first, then ordinary malloc. The send worker uploads one 16,384-byte part at a time. The details below link to the V2 contracts and client workers.

**Available commands:** The existing V2 build/flash command is:

    make app KIT=<kit> PORT=<port>

There is no scripted V2 communications case runner yet. Define this command for the cases below:

    make lab.v2-comms CASE=<case-id> KIT=<kit> PORT=<port> SERVER=<HOST_IP>:8777

This is a **proposed command, not an implemented target**. It must resolve the host IP as described above, verify that the V2 fixture server is reachable at `HOST_IP:8777`, run the named fixture, capture the request timeline and device memory probe, and write a run manifest/events/serial record. Do not use or start the LAB-16 server on port 8776 for these cases.
<a id="section-5"></a>
### Section 5 — V2 signed-in idle inbox baseline

**Scenario:** Establish the V2 memory state after the endpoint is signed in and its inbox worker has loaded an empty snapshot. Do not play media or send a message.

**Command to define:** CASE=signin-idle-baseline on the proposed V2 runner above. Existing prerequisite build/flash command: make app KIT=<kit> PORT=<port>.

**Run date/time:** NOT RUN. The [2026-10-25 startup capture](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt) stops at startup/login-worker readiness; it did not prove a successful V2 server response or signed-in inbox.

**Detailed description:** Use the desk credential to complete [POST /v2/session/login](../../../docs/product/communication-contracts/endpoint-health-and-login.md), then allow [POST /v2/session/refresh](../../../docs/product/communication-contracts/endpoint-health-and-login.md) and [GET /v2/inbox](../../../docs/product/communication-contracts/inbox-and-media.md) to run. Keep the inbox empty and wait through one 60-second refresh interval. Do not start audio/sketch playback or outgoing sends. This establishes the service and queue baseline for later V2 cases.

Sources: [endpoint health and login contract](../../../docs/product/communication-contracts/endpoint-health-and-login.md), [inbox and media contract](../../../docs/product/communication-contracts/inbox-and-media.md), [login HTTP worker](../../device/main/product_login_http_worker.cpp), [inbox HTTP worker](../../device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own pre-service baseline before Wi-Fi, SD, microphone, display, or workers add load.
- Join Wi-Fi and capture the IP-ready state.
- Complete PIN login; record request/response sizes and sample after the session is applied.
- Receive and apply the empty valid inbox snapshot; record body size, parse/apply peak, and result-buffer lifetime.
- Keep the empty inbox visible through one ordinary 60-second interval. Record health/session refresh and inbox GET separately, including their actual start/end order, queue/result counts, and buffer bytes/regions.
- Drain results, leave workers idle, and capture settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Wi-Fi, login, and empty inbox ready | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 60-second login refresh peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 60-second inbox poll/apply peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Workers idle after response consumption | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-6"></a>
### Section 6 — V2 single short audio-only inbox item

**Scenario:** Load one ordinary audio-only card and play a short message. This isolates the smallest normal audio card and media-fetch path.

**Command to define:** CASE=single-audio-3s on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return one inbox item with 3,000 ms of pcm_s16le audio: 96,000 media bytes at 16 kHz, mono, 16-bit. The item has no sketch descriptor. Select and play it to completion through [GET /v2/messages/{message_id}/media/audio](../../../docs/product/communication-contracts/inbox-and-media.md). Record actual snapshot JSON bytes, first media prefix, additional Range responses, response-buffer lifetime, and card allocations. The message/audio format is defined by [message delivery](../../../docs/product/communication-contracts/message-delivery.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive the one-item [GET /v2/inbox](../../../docs/product/communication-contracts/inbox-and-media.md) response; record exact bytes, parse/apply, and card-render peaks.
- Select the item; capture each audio Range request until the contiguous 32,000-byte playback prefix is available, then record playback start.
- Continue fetches through playback completion; record returned bytes, request count, active media-buffer count/bytes/region, and the highest memory sample.
- Stop playback, consume queued results, verify the complete 96,000-byte asset/hash, and measure after the worker and media buffers settle.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Inbox parse/apply and audio prefix | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback/media-fetch peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback complete and idle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-7"></a>
### Section 7 — V2 short audio-plus-sketch item

**Scenario:** Load and play a short item with both audio and a sketch. This measures the selected card's sketch download/replay path in addition to audio.

**Command to define:** CASE=single-audio-sketch-3s on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return one item with 3,000 ms of audio (96,000 PCM bytes) plus an 800-byte flsk2 sketch, representing 100 eight-byte points. Points use valid 0–119 x/y coordinates and nondecreasing t_ms values. Play the item using [GET /v2/messages/{message_id}/media/audio](../../../docs/product/communication-contracts/inbox-and-media.md) and render the sketch using [GET /v2/messages/{message_id}/media/sketch](../../../docs/product/communication-contracts/inbox-and-media.md). Do not preload media for unrelated cards. See [message delivery and flsk2](../../../docs/product/communication-contracts/message-delivery.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive, validate, apply, and render the item with both descriptors; record the snapshot bytes and allocations.
- Select the item and fetch its audio prefix and 800-byte sketch asset; log actual ordering and whether the single inbox worker serialized the requests.
- Start audio after its contiguous prefix is ready; replay all 100 sketch points and capture the active audio/sketch buffer high-water.
- Verify full media byte counts/hashes, stop playback, release/consume results, and measure recovered memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Inbox item applied | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Audio + sketch fetch/replay peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback ended and buffers released | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-8"></a>
### Section 8 — V2 long multi-range audio-plus-sketch playback

**Scenario:** Play a maximum-duration audio-plus-sketch message whose media must arrive in many bounded Range responses. Keep the selected item active through playback.

**Command to define:** CASE=long-range-audio-sketch on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Use the accepted maximum 180,000 ms audio duration: 5,760,000 bytes of 16 kHz mono signed 16-bit PCM. Attach a deterministic 3,600-point flsk2 sketch (28,800 bytes, 20 points per second over 180 seconds, nondecreasing timestamps). Fetch the assets through [GET /v2/messages/{message_id}/media/audio](../../../docs/product/communication-contracts/inbox-and-media.md) and [GET /v2/messages/{message_id}/media/sketch](../../../docs/product/communication-contracts/inbox-and-media.md). Playback can start after a contiguous 32,000-byte prefix; each media response is capped at 16,384 bytes by the current inbox worker. A complete audio fetch therefore requires at least 352 responses and the sketch at least 2 if the cap applies to every response; record actual counts. Measure media buffers as they grow and when they are released.

Sources: [accepted audio/sketch limits](../../../docs/product/communication-contracts/message-delivery.md), [Range/prefix playback](../../../docs/product/communication-contracts/inbox-and-media.md), [inbox worker buffer caps](../../device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive/apply the descriptor-only snapshot; record exact JSON bytes and card allocations.
- Select the message; capture audio and sketch Range headers/lengths, assemble the contiguous 32,000-byte audio prefix, then start playback.
- During all 180 seconds, record each Range request, cumulative/returned bytes, response-buffer lifetime, active media-buffer count/bytes/region, and periodic low-water samples. Do not imply requests overlap on the serial inbox worker.
- Verify all 5,760,000 audio bytes and 28,800 sketch bytes and their hashes; capture the final playback/asset release.
- Drain worker queues and buffers, then capture settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Initial prefix / playback start | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Mid-playback, media buffers at peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback complete and buffers drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-9"></a>
### Section 9 — V2 eight-item inbox across an eight-person hangout

**Scenario:** Load eight messages from members of an eight-person hangout and check card presentation when sender metadata is known, missing, or unrecognized.

**Command to define:** CASE=eight-member-inbox on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return a static eight-item inbox whose senders come from an eight-person hangout; the signed-in endpoint is one of those eight, so there may be at most seven other senders. Include known avatar/background IDs, missing IDs, and unrecognized IDs. Use a mix of audio-only and audio-plus-sketch summaries. Verify initials/default background fallback. Membership and revision mutation belong in Section 13. See [inbox presentation fields](../../../docs/product/communication-contracts/inbox-and-media.md) and [endpoint roster polling](../../../docs/product/communication-contracts/endpoint-health-and-login.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Sign in and load the static eight-item snapshot; record response bytes, parse/apply peak, and rendered-card count.
- Check known, missing, and unrecognized avatar/background values against the expected initials/background fallbacks.
- Confirm no unrelated audio or sketch media is fetched while rendering or navigating the eight cards.
- Leave the selected card unchanged, drain the inbox worker, and measure settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight-item response parse/apply | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight cards rendered with metadata fallbacks | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Settled inbox | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-10"></a>
### Section 10 — V2 maximum 64-item mixed inbox

**Scenario:** Load the maximum 64 supported inbox entries using a mix of audio-only and audio-plus-sketch messages. This measures card and snapshot scaling without loading every media asset.

**Command to define:** CASE=inbox-64-mixed on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return 64 valid items: 32 audio-only and 32 audio-plus-sketch, each with 3,000 ms audio (96,000 bytes) and each sketch descriptor pointing to an 800-byte, 100-point flsk2 asset. The audio and sketch bodies are media routes, not embedded in the snapshot; play only the selected item. Keep the complete JSON snapshot under the 262,144-byte limit with has_more=false and next_cursor=null. Record the actual JSON body size and whether nonselected media is fetched.

Sources: [complete inbox rules and 262,144-byte cap](../../../docs/product/communication-contracts/inbox-and-media.md), [media formats](../../../docs/product/communication-contracts/message-delivery.md), [inbox carousel client](../../device/main/product_inbox_bridge.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive the 64-item snapshot; sample response receipt, parse, apply, and render.
- Scroll through the carousel without opening media for all entries.
- Play one selected card only and record incremental media memory.
- Return to idle; count retained card images/objects and heap recovery.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 64-item response receipt/parse | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 64 cards applied and rendered | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| One selected item playing | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Returned to idle inbox | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
<a id="section-11"></a>
### Section 11 — V2 64-item all-sketch card stress

**Scenario:** Load 64 messages that all have sketches, then play one selected item. This stresses the card/image path while checking whether sketch playback storage is limited to the active card.

**Command to define:** CASE=inbox-64-all-sketch on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return 64 valid audio-plus-sketch items. Each has a 3,000 ms audio descriptor (96,000 bytes) and an 800-byte flsk2 sketch (100 points); do not embed media in the inbox JSON and do not fetch all 64 sketches up front. Exercise carousel creation and selecting cards, then load/replay a sketch only for the active item. This distinguishes per-card thumbnail/image allocations from a full sketch playback buffer. The current contract does not define a maximum sketch asset size; this fixture's 800-byte choice is explicit test data, not a product limit.

Sources: [inbox item/media contract](../../../docs/product/communication-contracts/inbox-and-media.md), [flsk2 encoding](../../../docs/product/communication-contracts/message-delivery.md), [inbox card and sketch ownership](../../device/main/product_inbox_bridge.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive, parse, apply, and render all 64 sketch descriptors; record JSON bytes and rendered-card count without fetching sketch assets.
- Navigate the cards without opening media; verify that no inactive sketch is fetched or held as a playback buffer.
- Select one card and load/replay only its 800-byte sketch; record active buffer and draw/replay high-water.
- Switch to another card and verify the old active sketch storage is released or reused; stop and measure retained card plus active-sketch memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 64 cards with sketch descriptors | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Selected sketch download and replay peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Switch/stop and settle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-12"></a>
### Section 12 — V2 inbox response near the 262,144-byte ceiling

**Scenario:** Measure the response-buffer and parser high-water using a valid inbox JSON body close to the maximum accepted response size.

**Command to define:** CASE=inbox-near-limit on the proposed V2 runner above.

**Run date/time:** NOT RUN. Fixture creation is OPEN until the generator can produce a schema-valid near-limit body without adding unsupported padding fields.

**Detailed description:** Target a valid response body of 256,000 bytes (250 KiB), below the accepted 262,144-byte cap. Use only allowed inbox fields and valid media descriptors; record the exact byte count sent and received. Unknown padding fields are not allowed. If the fixture cannot reach the target with valid schema data, stop and report the highest valid size instead of weakening validation. Exercise response allocation, JSON parse, apply, render, and release. Exact-limit and over-limit semantics are covered separately by Section 12a so this fixture has one target size.

Sources: [inbox body cap and validation](../../../docs/product/communication-contracts/inbox-and-media.md), [inbox response allocation](../../device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Record the exact content length at the server and client.
- Capture while the response buffer is live and parsing is underway.
- Capture after apply/render and after the response buffer is freed.
- Confirm the 256,000-byte valid snapshot is accepted and applied; record exact item count and hash/size evidence.
- If the schema-valid fixture cannot reach 256,000 bytes, keep the target OPEN and report the largest valid body. Do not pad with unsupported fields or mark the target case PASS at a smaller size.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Response allocation / parse active | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Snapshot applied and rendered | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Buffers released / idle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-12-boundary"></a>
### Section 12a — V2 exact inbox response limit boundary

**Scenario:** Verify the inclusive 262,144-byte inbox response ceiling and the over-limit rejection behavior against a previously applied valid inbox.

**Command to define:** `CASE=inbox-response-boundary` on the proposed V2 runner above.

**Run date/time:** NOT RUN; no boundary-case run or fixture exists. This case needs a schema-valid generator that can produce both exact-limit and one-byte-over bodies using supported fields only.

**Detailed description:** Start with a known valid cached inbox. Return a complete valid JSON snapshot of exactly 262,144 bytes; the accepted contract allows a body at the ceiling. Then return a schema-valid body of 262,145 bytes. The exact-limit response must apply; the oversized response must be rejected atomically, leave cached membership and selection intact, and enter connecting. Use permitted varying field values or item data to reach the byte counts. Do not add unknown padding fields. If either exact body size cannot be generated without violating the schema, mark fixture construction OPEN and stop.

Source: [accepted inbox byte ceiling and over-limit behavior](../../../docs/product/communication-contracts/inbox-and-media.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline; apply a small valid cached inbox first.
- Receive the exact 262,144-byte snapshot; record server/client byte counts, response allocation, parse/apply result, selected item, and memory peak.
- Receive the 262,145-byte snapshot; record byte counts, rejection/connecting state, and prove the previous membership and selection remain unchanged.
- Return a valid recovery snapshot and capture its apply/result-buffer lifetime.
- Drain queues and buffers and capture settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Fresh baseline and small cached inbox | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Exact 262,144-byte response parse/apply | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 262,145-byte response rejected | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Valid recovery and buffers drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-13"></a>
### Section 13 — V2 inbox revision add, equal, and stale responses

**Scenario:** Change inbox membership and sender presentation while signed in. Confirm a newer snapshot adds a card, an equal revision can update presentation, and a lower revision cannot replace newer membership.

**Command to define:** CASE=inbox-revision-update on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Start at inbox_revision 100 with eight items. Add one item and return revision 101. Return revision 101 again with a changed sender name/avatar/background for an existing message. Then return stale revision 100. Requests occur through [GET /v2/inbox](../../../docs/product/communication-contracts/inbox-and-media.md) on its normal 60-second schedule or an accepted trigger; there is no push channel. Measure response buffers and the rendered card set at each apply.

Sources: [revision and sender presentation rules](../../../docs/product/communication-contracts/inbox-and-media.md), [inbox client](../../device/main/product_inbox_bridge.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Apply revision 100 and record the eight-card baseline.
- Apply revision 101 with a ninth card; selection must not move.
- Apply equal revision 101 with sender name/avatar/background changed; membership/order/selection remain, matching-card presentation updates.
- Apply stale revision 100 with a changed sender field; it must not replace membership or update presentation.
- Settle and record heap/queue recovery.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight-item revision 100 | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Revision 101 adds ninth item | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Equal/stale revision handling peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Settled after apply | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-14"></a>
### Section 14 — V2 playback, media ranges, last-viewed, and inbox refresh

**Scenario:** Play one long, finished audio-plus-sketch item while the inbox's scheduled refresh becomes due. Let the selected item settle so the one-second last-viewed write is also exercised.

**Command to define:** CASE=playback-inbox-refresh on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Use the 180-second item from Section 8: 5,760,000 PCM bytes and its deterministic flsk2 sketch. Start playback from a 32,000-byte contiguous prefix. Continue Range requests through [GET /v2/messages/{message_id}/media/audio](../../../docs/product/communication-contracts/inbox-and-media.md) and allow one scheduled 60-second [GET /v2/inbox](../../../docs/product/communication-contracts/inbox-and-media.md) while playback continues. Keep selection stable for at least one second to cause one [PUT /v2/inbox/last-viewed](../../../docs/product/communication-contracts/inbox-and-media.md). Inbox, media, and last-viewed calls share one serial inbox worker; record due, enqueue, start, and completion times, queue order, and response-buffer lifetime. The test measures scheduling and queueing, not simultaneous HTTP requests on that worker.

Sources: [inbox polling and last-viewed contract](../../../docs/product/communication-contracts/inbox-and-media.md), [media formats/limits](../../../docs/product/communication-contracts/message-delivery.md), [inbox HTTP worker](../../device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Start 180-second audio-plus-sketch playback and record active media buffers.
- Keep playback active across the next refresh deadline; record the inbox GET's due time, queue delay, actual start/end, and effect on subsequent media Range reads.
- Keep selection still for one second; record the last-viewed PUT's due time and actual serialized position relative to media/inbox work.
- Record worker queue/result occupancy and every outstanding response buffer through the sequence; do not label same-worker jobs as concurrent.
- Finish or stop playback, consume results, release media buffers, then sample settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback active before refresh | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Refresh/media/last-viewed queue peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback stopped, worker drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-15"></a>
### Section 15 — V2 outgoing audio-only message

**Scenario:** Record and send a finished audio-only message to two frozen hangout recipients, then return to the inbox. Keep normal inbox refresh enabled and observe any refresh that naturally falls during upload; this case alone does not claim cross-worker overlap.

**Command to define:** CASE=send-audio-only on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 send run has been captured against the V2 server.

**Detailed description:** Send an 8,000 ms pcm_s16le item: 256,000 audio bytes, no sketch, to two frozen recipients. The part size is 16,384 bytes, so the audio uses 16 sequential parts: fifteen full 16,384-byte parts and a final 10,240-byte part, followed by complete. After finish, return to the inbox while the send worker continues. Keep the 60-second inbox refresh and health/session requests enabled. See [message create/part/complete routes](../../../docs/product/communication-contracts/message-delivery.md), [session and health routes](../../../docs/product/communication-contracts/endpoint-health-and-login.md), and [send HTTP worker](../../device/main/product_send_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Record the 8-second message, freeze two recipient IDs, and finish.
- [POST /v2/outgoing](../../../docs/product/communication-contracts/message-delivery.md); upload audio parts sequentially with [PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}](../../../docs/product/communication-contracts/message-delivery.md); then [POST /v2/outgoing/{message_id}/complete](../../../docs/product/communication-contracts/message-delivery.md).
- Return to inbox and record whether its scheduled GET actually falls while the send worker is active; include due/start/end times and queue/result-buffer lifetime. If upload finishes first, mark only the overlap observation INCOMPLETE; Section 17 owns the guaranteed worker-overlap case.
- Record final receipt/status, recipient set, part count, and memory after the send worker drains; if the complete response is lost, use [GET /v2/outgoing/by-client-id/{client_message_id}](../../../docs/product/communication-contracts/message-delivery.md) to resolve status.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Recording/finish and send creation | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Audio part upload; observe inbox/health refresh if due | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Complete accepted and send drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-16"></a>
### Section 16 — V2 outgoing audio-plus-sketch message

**Scenario:** Send one finished audio-plus-sketch item and measure both media-part streams. The sketch contains a deterministic pattern rather than an unbounded drawing. Keep inbox refresh enabled and report an overlap only if a refresh is observed during the upload.

**Command to define:** CASE=send-audio-sketch on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 send run has been captured against the V2 server.

**Detailed description:** Send 8,000 ms of audio (256,000 PCM bytes) plus an 800-byte flsk2 sketch (100 eight-byte points) to two frozen recipients. Audio uses sixteen 16,384-byte-or-smaller parts; the sketch uses one part. Upload starts only after recording has finished. Return to inbox while the send worker completes. Verify the server accepts the recipient set atomically and the sender does not receive a self-copy.

Sources: [message delivery routes and flsk2](../../../docs/product/communication-contracts/message-delivery.md), [send implementation](../../device/main/product_send_http_worker.cpp), [outbox builder](../../shared/message_delivery/fl_product_send/outbox.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Record audio and 100 sketch points; stop and freeze recipients.
- [POST /v2/outgoing](../../../docs/product/communication-contracts/message-delivery.md); upload 16 audio parts and one sketch part serially with [PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}](../../../docs/product/communication-contracts/message-delivery.md); then [POST /v2/outgoing/{message_id}/complete](../../../docs/product/communication-contracts/message-delivery.md).
- Return to the inbox; record whether a scheduled [GET /v2/inbox](../../../docs/product/communication-contracts/inbox-and-media.md) or health/session exchange actually lands during upload, with queue and response-buffer lifetimes.
- Confirm accepted receipt, recipient publication, no self-copy, and memory recovery; if the complete response is lost, use [GET /v2/outgoing/by-client-id/{client_message_id}](../../../docs/product/communication-contracts/message-delivery.md) to resolve status.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Recording and sketch capture | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Audio/sketch part upload peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Complete accepted and send drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-17"></a>
### Section 17 — V2 health, session refresh, inbox refresh, and send overlap

**Scenario:** Run the three HTTP workers under their ordinary periodic schedules while one finished message is uploading. This measures cross-worker high-water and queued-result lifetime under the deterministic overlap fixture profile.

**Command to define:** CASE=worker-overlap on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 concurrency run has been captured against the V2 server.

**Detailed description:** Keep an endpoint signed in and displaying its inbox. [POST /v2/endpoint/health](../../../docs/product/communication-contracts/endpoint-health-and-login.md) and [POST /v2/session/refresh](../../../docs/product/communication-contracts/endpoint-health-and-login.md) share the login worker and serialize there; [GET /v2/inbox](../../../docs/product/communication-contracts/inbox-and-media.md) uses the inbox worker. The scenario must produce at least one observed interval with an exchange active on each of the login, inbox, and send workers. Use a deterministic 180-second outbound audio-plus-sketch fixture (5,760,000 audio bytes plus 28,800 sketch bytes) and a versioned fixture-server profile that delays each part response by 1 second, keeping each response within the configured client timeout. This keeps send active across multiple ordinary 60-second deadlines; the induced delay is test input, not a production latency claim. Record every request's due/enqueue/start/end time, actual overlap count, queue/result-buffer lifetime, and response bytes. If the worker overlap is not observed or the delay exceeds a client timeout, mark the scenario INCOMPLETE and preserve the actual sequence; do not claim the intended overlap.

Sources: [health/login worker contract](../../../docs/product/communication-contracts/endpoint-health-and-login.md), [inbox/media worker contract](../../../docs/product/communication-contracts/inbox-and-media.md), [message delivery contract](../../../docs/product/communication-contracts/message-delivery.md), [login HTTP worker](../../device/main/product_login_http_worker.cpp), [inbox worker](../../device/main/product_inbox_http_worker.cpp), [send worker](../../device/main/product_send_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Sign in and load the inbox; capture a clean ready/idle state and the next due times for login-worker and inbox-worker schedules.
- Start the deterministic 180-second outbound capture and send with the versioned 1-second-per-part fixture profile; record the recording and upload beats separately.
- Observe multiple health, session-refresh, and inbox-refresh deadlines while the send worker is active; record login-worker serialization and inbox-worker queue order.
- Prove at least one interval with one exchange in flight on each of the three workers; record start/end intervals, response buffers, and maximum in-flight exchanges.
- Verify complete/receipt state and that every result buffer is consumed/released; wait for all workers and queues to settle and measure recovery.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 180-second recording and finished payload queued | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Three-worker overlap during delayed part upload | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Health/session serialization and result queues | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| All workers idle and buffers released | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="section-18"></a>
### Section 18 — V2 signed-out hangout roster sizes

**Scenario:** Measure the signed-out roster path with zero users, one user, and eight users. Verify that each response updates the roster at the next poll without treating it as an inbox push.

**Command to define:** CASE=hangout-roster-0-1-8 on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 roster scenario has been captured against the V2 server.

**Detailed description:** Keep the endpoint signed out. Return valid [GET /v2/hangout](../../../docs/product/communication-contracts/endpoint-health-and-login.md) responses with 0, then 1, then 8 members on separate poll cycles, followed by a response with one member removed (7 remain). Check that cards are added/removed only after each response applies. Active roster polling is normally every 60 seconds. Do not run this as a signed-in inbox event: V2 uses separate readiness/roster and inbox exchanges. The five-minute poll after ten quiet minutes is a separate cadence observation and is not required for the 0/1/8/removal case.

Sources: [hangout readiness and roster contract](../../../docs/product/communication-contracts/endpoint-health-and-login.md), [login HTTP worker](../../device/main/product_login_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline; verify signed-out state and no inbox request.
- Return the empty roster; record response bytes, apply/render peak, and zero-card state.
- Return one member on the next poll; verify one selectable card and capture memory.
- Return eight members; include known and fallback avatar/background metadata, verify all cards, and capture memory.
- Return seven members after removing one; verify the removal only after the valid response applies and capture settled memory.
- Keep the endpoint signed out throughout and confirm no inbox traffic. Record any idle-cadence probe separately from this case.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Empty roster response | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| One-member roster | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight-member roster and presentation fallbacks | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Seven-member roster after removal | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Roster worker idle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

## Single-scenario orchestrator prompt

Copy this prompt for one scenario at a time. Replace the bracketed fields before handing it to an implementation agent.

```text
You are the orchestrator for exactly one scenario in the device-memory lab campaign.

Scenario: [section number and title]
Variant/case ID: [registered VARIANT or proposed CASE]
Workspace: [repository/worktree]

The LAB-16 and V2 campaign README is the source of truth for the scenario, fixture, beats, measurements, tolerance rules, and evidence. Do not change neighboring scenarios or silently change product behavior to fit a fixture. You are authorized to delegate and complete the bounded phases below.

Delegate each phase to a suitable subagent: implementer, fresh read-only implementation verifier, scenario executor, fresh read-only results verifier for each run, and evidence/documentation recorder. Keep these responsibilities separate. A verifier must not verify its own work, and the executor must not verify its own run. The orchestrator owns the phase gates and applies the run limits below.

1. IMPLEMENT AND VERIFY
   - Have an implementation subagent implement this scenario only, including its declared variant/case, versioned source fixture, runner wiring, required beats/measurements, and evidence output.
   - Have a fresh read-only implementation verifier check the changes against this README, linked contracts, fixture identity, build/command path, and required evidence. Include appropriate build and implementation checks.
   - You are authorized for up to three implement/verify cycles total, stopping as soon as verification passes. If verification still fails after cycle three, record the unresolved findings and stop before scenario execution.

2. EXECUTE AND VERIFY RESULTS
   - Before the first run, confirm preconditions, the exact build/configuration, variant and fixture revision/hash, the scenario's required beats, and applicable repeatability tolerances. Use the defaults in this README; a scenario may be stricter only if that was declared before the first run. Do not invent or widen tolerances after seeing results.
   - Execute the scenario at least twice. Each execution gets a fresh boot, unique run ID, same verified build and fixture revision, complete required beat capture, and a new server process owned by that run when the scenario uses a host server. Resolve the endpoint using the host-IP procedure in this README.
   - For each run, immediately preserve its immutable evidence bundle, including incomplete or failed runs. Then assign a fresh read-only results verifier to check the manifest bounds and identities, raw device/host evidence, required measurements and beats, acceptance criteria, and the stated outcome. Do not accept a completion marker alone as proof.
   - After the first two runs are complete and independently verified, compare every required measurement using this README's tolerances. A third scenario execution and independent result verification are authorized only if at least three required numeric measurements exceed tolerance across at least two measurement groups. Do not run a third merely to increase confidence or to replace an incomplete first/second run. Do not exceed three scenario executions under this authorization.
   - If either of the first two runs is incomplete, or the mismatch does not meet the third-run trigger, preserve the evidence and report the result as OPEN/INCOMPLETE with the reason. Ask for additional run authorization if another run is needed.

3. RECORD THE SCENARIO RESULT
   - Always keep each run's raw evidence linked, regardless of whether the runs agree.
   - Update the scenario's authoritative result summary only when at least two complete, independently verified runs agree across all required measurements: every numeric value is within its declared tolerance and all exact fields match. If a third run was authorized, a single pair must agree across the full measurement set; retain and report all three runs.
   - Record the accepted pair, per-run values, differences, tolerances, functional verdict, and links to each manifest, events, metrics, serial capture, and server log. Mark PASS only when both repeatability and the scenario's functional criteria pass. Record a stable failure as FAIL; otherwise keep OPEN/INCOMPLETE. Never overwrite or edit an immutable run bundle.

Finish with a concise report of implementation cycles, independent verification findings, run IDs and statuses, comparison/tolerance result, updated document location, and anything still OPEN. Stop at the authorization limits above.
```

## Implementation handoff order

1. **Repair LAB-16 evidence first.** Preserve multipart boundaries in the diary parser and verify exact received chunk sizes/content hashes. Add same-boot memory probes to all four existing scenario builds. Then rerun Sections 1–4 individually and link each new manifest, serial log, server log, per-beat events, and memory metrics here.
2. **Build the V2 measurement runner.** Register its lab identity/evidence slug and implement the proposed `make lab.v2-comms CASE=...` command. It must collect the exact-build startup baseline and case beats on one boot, use the V2 routes, and emit the common memory fields plus request/buffer telemetry.
3. **Version the V2 fixture sources.** Add a deterministic fixture/generator directory per case as defined above. Resolve the 256,000-byte and exact/over-limit bodies using supported schema fields only. Implement the one-second part-delay profile for Section 17 as a named fixture input, not an undeclared environment tweak.
4. **Execute Sections 5–18 one case at a time.** Preserve each case's run ID and exact manifest bounds. Record `NOT RUN`, `NOT CAPTURED`, `OPEN`, or `INCOMPLETE` until its evidence supports a stronger claim; update the matching section with the run artifact links after each capture.

## Current findings and what remains to run

- Section 0 is measured on the V2 product build: before storage, microphone, display, and Wi-Fi load, it had 145.27 KiB free in the internal SRAM heap, 137.69 KiB free in its DMA-capable internal SRAM subset, a largest free DMA block of 62 KiB, and 16,287.67 KiB free in PSRAM.
- The same V2 boot reached 18.76 KiB free in the DMA-capable internal SRAM query and an 18 KiB largest free block after Wi-Fi and login-worker startup. It never entered Record and never confirmed a response from the intended host-IP endpoint on port 8777.
- The four LAB-16 scenarios listed above have functional pass evidence, but no phase-specific SRAM/PSRAM heap measurements or same-boot clean baselines.
- LAB-16 needs same-boot memory probes in all four scenario variants before its next run can answer how much memory is free while recording, writing SD, uploading, or recovering. Each of the four variants requires a new memory run.
- The V2 communications cases, including the separate exact-limit boundary case, are all NOT RUN. The proposed runner, registry/evidence identity, deterministic fixture controls, and per-phase reports still need implementation.
- The exact-limit fixture (Section 12a) and delayed-part profile for the three-worker overlap case (Section 17) are explicit implementation inputs; they do not yet exist.
- LAB-16 historical server chunk counts do not prove payload integrity until the multipart parser/verifier issue above is resolved.
- Keep the V2 product and LAB-16 results separate: LAB-16 has no LVGL, inbox, carousel, Record canvas, or V2 HTTP workers.

## V2 contract and implementation references

- [Endpoint health, login, roster, and refresh](../../../docs/product/communication-contracts/endpoint-health-and-login.md)
- [Inbox, media, Range playback, and last-viewed](../../../docs/product/communication-contracts/inbox-and-media.md)
- [Outgoing message parts and flsk2 sketch encoding](../../../docs/product/communication-contracts/message-delivery.md)
- [V2 device build and diagnostic instructions](../../device/README.md)
- [Product memory probe](../../device/main/product_mem_probe.c)
- [Inbox HTTP worker](../../device/main/product_inbox_http_worker.cpp)
- [LAB-16 firmware variants](main/app_main.c)
- [LAB-16 WAV upload code](main/lab16_upload.c)
