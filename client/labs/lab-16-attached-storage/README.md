# LAB-16 attached-storage memory campaign

Last updated: 2026-10-25

Source of truth for each `VARIANT=` on `make lab.lab16`: scenario beats, fixtures, measurement tables, and evidence for the scripted SD + microphone lab (diary server on port **8776**).

**Implementers and agents:** [AGENTS.md](AGENTS.md).

## Table of contents

- [Agent and implementer guide](AGENTS.md)
- [VARIANT `upload` — record to SD, then upload](#variant-upload)
- [VARIANT `server-offline-during-record` — server offline during recording](#variant-server-offline-during-record)
- [VARIANT `overlap-record-during-upload` — record message 2 while message 1 uploads](#variant-overlap-record-during-upload)
- [VARIANT `live-stream-failover-during-record` — live outage, SD fallback, and drain](#variant-live-stream-failover-during-record)
- [Published memory results (docs)](../../../docs/labs/evidence/lab-16/README.md)


**Same-boot baseline:** Add the capability probe to all four LAB-16 scenario builds and emit the initial sample after `LAB16_BOOT status=stable` but before `sd_session_open`, microphone/I2S initialization, or Wi-Fi. Do not substitute another firmware image or an earlier run as the baseline. Until each VARIANT captures that sample on the same boot as its scenario beats, mark the baseline NOT CAPTURED.

<a id="variant-upload"></a>
## VARIANT `upload` — LAB-16 upload: record to SD, then upload

**Scenario:** The device records microphone PCM to the attached SD card first. After the recording is complete and validated, it joins Wi-Fi and uploads the stored recording as one-second WAV chunks.

**Command:**

    make lab.lab16 VARIANT=upload KIT=lynn-kit PORT=COM5

This is the registered default upload variant. The build uses `sdkconfig.defaults.upload` and its isolated `build/labs/lab-16-attached-storage-upload` directory; the default `make lab.lab16` selection remains `upload`.

**Preconditions:** Use the configured LAB-16 kit, a mounted SENSOR microSD with enough capacity, working kit Wi-Fi, and a host route reachable from that kit. Before flashing, resolve the host IPv4 address with `client_address()` or set `LAB16_CLIENT_ADDRESS` to a verified reachable address. The runner records the address and whether route discovery or the override supplied it, and uses that same address in the firmware build and manifest. Each run starts a new run-owned diary server before flashing.

**Versioned source fixture:** [upload fixture](../../../server/labs/lab-16-attached-storage/fixtures/upload/fixture.json), ID `lab16-record-to-sd-then-upload`, revision `lab16-upload/v2`, schema `lab16-upload-fixture/v1`, generator `static-json/v1`, seed `not-applicable; captured PCM is device input and is not stored in the fixture`. Its exact 915-byte LF-only source has SHA-256 `08c6d5b6a49fa0e80d33fd26120299ad89f0071c9a86f7ebc1f35a673e720f28`; the runner records and verifies that hash in each run manifest. The fixture fixes session 1, 210 ordered one-second chunks, 32,000 PCM bytes and 32,044 WAV bytes per request, 50 ms memory sampling, boundary/field names, and HTTP 200 acceptance.

**Historical functional run:** PASS. UTC bounds: [2026-10-23 05:41:36.831692–05:45:59.313600](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/run-manifest.json). Run ID: [20261023-upload-run-b193b616](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/run-manifest.json). A later interrupted attempt, [20261024-upload-run-c4c3662e](../../../docs/labs/evidence/lab-16/runs/20261024-upload-run-c4c3662e/run-manifest.json), ended UNMEASURED and does not replace the PASS run.

**Detailed description:** After a 500 ms post-flash settle, the firmware probes the SENSOR microSD with an LBA0 read, records 210 seconds of 16 kHz mono signed 16-bit PCM starting at LBA 2048, and validates the stored header plus first/middle/last samples. The capture is 6,720,000 PCM bytes (6,562.5 KiB), or 32,000 PCM bytes per second. It then connects to the LAB-16 diary server at port 8776 and uploads 210 one-second WAV requests. Each request contains 32,000 PCM bytes plus a 44-byte WAV header, 32,044 bytes total. Posts use the LAB-16 v1 diary wire format.

Sources: [LAB-16 firmware flow](main/app_main.c), [WAV/chunk uploader](main/lab16_upload.c), [LAB-16 diary server](../../../server/labs/lab-16-attached-storage/server.py).

**Ordered beats and measurements:** The capture build samples internal 8-bit heap, DMA-capable internal heap (a subset), and PSRAM every 50 ms, aggregates readings in RAM, and writes one summary per named beat without serial output on each tick. Each summary records region totals, current free/largest blocks, interval minimum free/largest blocks with their monotonic sample timestamps, cumulative minimum-free watermarks since heap initialization, and each watermark's delta from the same-boot clean baseline. It records monotonic window bounds and counts, PCM bytes, SD writes, and task stack high-water for `main`, `lab16_mem`, `sys_evt`, `wifi`, and lwIP's `tiT`, including each task name, configured stack size, allocated stack size, and the SDK-returned unit (`StackType_t` items; one byte per item on this Xtensa target). For this ESP-IDF build, `main` is configured for 12,288 bytes and allocates 12,800; `lab16_mem` is 4,096; `sys_evt` is configured for 2,304 and allocates 2,816; the Wi-Fi driver task is `wifi` with 6,656 bytes, derived from the bundled ESP-IDF 5.4.2 Wi-Fi stack-size helper with WPA3-SAE enabled and newlib nano format disabled; and lwIP's task is named `tiT`, configured for 3,072 and allocates 3,584. A system task that has not started yet is labeled `not_yet_started`; its stack high-water is captured in later windows after creation. The default upload runs synchronously on `main`, so its upload stack use is included in the `main` high-water measurement. Upload telemetry records application buffer allocation/free boundaries and region, request/response byte counts and latency, in-flight requests, retries, failures, and acceptance counts. Device response-body bytes count bytes actually read and discarded from the HTTP client; Content-Length is retained separately for comparison, and response content is not persisted.

- `baseline_before_sd`: after stable boot and the 500 ms settle, before the SD mount, microphone/I2S, and Wi-Fi.
- `sd_mount_probe`: mount/probe the SENSOR card; retain capacity, bus width/frequency, and successful LBA0 read evidence.
- `mic_i2s_open_start` and `mic_i2s_open_complete`: direct memory samples immediately before microphone/I2S initialization and after it opens.
- `sd_mic_recording`: sample immediately before the first SD header write, then throughout recording; retain interval low-water values while SD writes are active.
- Finish 210 seconds / 6,720,000 PCM bytes; record header, first/middle/last readback, signal metrics, and `silent=0` validation.
- `wifi_upload_active`: begins before Wi-Fi join and spans all 210 one-second posts; retain every request attempt/response and sustained upload low-water.
- `upload_complete_workers_quiescent`: after upload returns and the SD session closes, capture recovery memory and stop the sampler cleanly.

**Functional acceptance:** Confirm a detected card with no probe writes, exactly 6,720,000 on-card PCM bytes and valid non-silent header/first/middle/last readback, and exactly 210 successful session-1 requests with 32,044 WAV / 32,000 PCM bytes each. For every sequence, compare the SHA-256 of PCM read back from SD on the device with the host's received PCM hash; retain the WAV hash, actual request-body length, actual response-body bytes read, Content-Length, response status, and run-owned server rows/log. Require zero failed requests, the complete 0–209 server sequence set, and clean upload/SD/sampler quiescence. Allocation/free events report outstanding buffers after the actual allocation or free, respectively. A completion marker alone is insufficient.

**Repeatability rules for the first run and all later pairs:** Use the inherited campaign defaults in [AGENTS.md](AGENTS.md); this scenario declares no stricter tolerance. Compare corresponding per-beat/window internal and DMA free/minimum-free and largest-block readings with `max(1 KiB, 5% of pair mean)`; compare PSRAM free/minimum-free and largest-block readings with `max(16 KiB, 0.5% of pair mean)`. Compare per-task stack high-water values, converted to bytes using their recorded SDK units, with `max(1 KiB, 5% of pair mean)`. Compare each low-water sample-time offset from its beat start, stream duration, and request latency with `max(250 ms, 10% of pair mean)`. Fixture byte counts, chunk sequence/session, hashes, response sizes/statuses, variant/build/fixture identity, buffer lifecycle facts, and outstanding/in-flight/retry/failure counts must match exactly. Apply the AGENTS.md campaign gate for authorizing a third run; do not widen thresholds after capture.

**Run evidence:** [manifest](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/run-manifest.json) · [events](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/events.jsonl) · [serial](../../../logs/device/labs/lab-16/20261023-upload-run-b193b616/serial.txt) · [server log](../../../docs/labs/evidence/lab-16/runs/20261023-upload-run-b193b616/server.log).

**Implementation gate:** OPEN after cycle 3. The verifier confirmed that, on `KeyboardInterrupt` during capture, partial metrics are preserved but the adapter exception path omits `server.stop()` ([`lab_adapter_lab16.py`](../../../scripts/lab_adapter_lab16.py)). This may leave this run's diary server bound to port 8776; the next run cannot safely start its own server. Scenario execution was not started.

**Memory evidence:** CAPTURED. Pair [20261025-upload-manual-01](../../../docs/labs/evidence/lab-16/runs/20261025-upload-manual-01/run-manifest.json) and [20261025-upload-manual-02](../../../docs/labs/evidence/lab-16/runs/20261025-upload-manual-02/run-manifest.json) (`lab16-fw-v5-upload-memory`, fixture `lab16-upload/v2`). Repeatability compare: `make lab.lab16.compare RUN_A=20261025-upload-manual-01 RUN_B=20261025-upload-manual-02` → PASS.

**Evidence output:** Each execution writes an immutable run bundle under `docs/labs/evidence/lab-16/runs/<run_id>/` with manifest, events, metrics, server log, and notes, plus raw serial under `logs/device/labs/lab-16/<run_id>/serial.txt`. The host parser is [parse_upload_evidence.py](tools/parse_upload_evidence.py); pair compare is [compare_upload_runs.py](tools/compare_upload_runs.py). **Published KiB tables** live under [docs/labs/evidence/lab-16/results/upload.md](../../../docs/labs/evidence/lab-16/results/upload.md); regenerate with `make lab.lab16.results` after new captures.

**Measurements (KiB):** See [campaign summary table](../../../docs/labs/evidence/lab-16/results/upload.md#campaign-summary) (from `metrics.json` on the accepted pair). Full per-beat windows and repeatability output are in the same doc.
<a id="variant-server-offline-during-record"></a>
## VARIANT `server-offline-during-record` — LAB-16 server offline during recording

**Scenario:** The device records the full message to SD while the server is not accepting connections. The server comes up only after recording has finished; the device then uploads the stored audio.

**Command:**

    make lab.lab16 VARIANT=server-offline-during-record KIT=lynn-kit PORT=COM5

**Historical functional run:** PASS. UTC bounds: [2026-10-23 05:46:06.603422–05:50:25.274147](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/run-manifest.json). Run ID: [20261023-server-offline-during-record-run-d9566b03](../../../docs/labs/evidence/lab-16/runs/20261023-server-offline-during-record-run-d9566b03/run-manifest.json).

**Detailed description:** This is the same 210-second, 16 kHz mono, signed 16-bit capture as VARIANT `upload`: 6,720,000 PCM bytes to SD beginning at LBA 2048, followed by audio validation and 210 one-second WAV posts. The host delays starting the diary server until the firmware reports the SD recording complete. It checks that storage capture does not depend on server availability during the recording phase.

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

**Measurements (KiB):** [results/server-offline-during-record.md](../../../docs/labs/evidence/lab-16/results/server-offline-during-record.md) (placeholder until a memory run exists).

<a id="variant-overlap-record-during-upload"></a>
## VARIANT `overlap-record-during-upload` — LAB-16 record message 2 while message 1 uploads

**Scenario:** The device finishes one 180-second message and starts uploading it. While that upload runs, it records a second 180-second message to SD.

**Command:**

    make lab.lab16 VARIANT=overlap-record-during-upload KIT=lynn-kit PORT=COM5

**Historical functional run:** PASS. UTC bounds: [2026-10-23 06:15:00.446797–06:22:02.027734](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/run-manifest.json). Run ID: [20261023-overlap-record-during-upload-run-8d644d2f](../../../docs/labs/evidence/lab-16/runs/20261023-overlap-record-during-upload-run-8d644d2f/run-manifest.json).

**Detailed description:** Each message is 180 seconds of 16 kHz mono signed 16-bit PCM: 5,760,000 bytes (5,625 KiB) per message, 11,520,000 PCM bytes across both. Message 1 is recorded to SD and uploaded as 180 one-second WAV chunks while message 2 is being recorded to a different SD LBA. Message 2 is then read back and uploaded as a second 180-chunk session. This variant stresses simultaneous old-message upload and new-message recording on the LAB-16 diary path.

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

**Measurements (KiB):** [results/overlap-record-during-upload.md](../../../docs/labs/evidence/lab-16/results/overlap-record-during-upload.md) (placeholder until a memory run exists).

<a id="variant-live-stream-failover-during-record"></a>
## VARIANT `live-stream-failover-during-record` — LAB-16 live upload with outage, SD fallback, and drain

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

**Measurements (KiB):** [results/live-stream-failover-during-record.md](../../../docs/labs/evidence/lab-16/results/live-stream-failover-during-record.md) (placeholder until a memory run exists).


## Current findings and what remains to run

- VARIANT `upload` has an accepted memory pair; tables are in [docs/labs/evidence/lab-16/results/upload.md](../../../docs/labs/evidence/lab-16/results/upload.md).
- The other three VARIANTs have functional pass evidence only; memory tables remain NOT CAPTURED until instrumented reruns.
- LAB-16 historical server chunk counts do not prove payload integrity until the multipart parser/verifier issue on VARIANT `live-stream-failover-during-record` is resolved (see that section).

Implementation contract and orchestration: [AGENTS.md](AGENTS.md).
