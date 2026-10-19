# LAB-03/v2 — audio/sketch playback evaluation

Candidate implementation and target trace evidence are complete for the scripted paths and a 600.09-second sustained run. **Performance verdict: INCOMPLETE.** No synchronized camera/microphone was available; first audible sample, DAC position, actual LCD timing and underruns remain unmeasured. All thresholds below are **provisional lab criteria**, not approved product budgets.

## Identity and reproduction

- Final run: `20261017-lab03-scripted-06`; registered kit `lynn` / BOX-3, ESP32-S3, 320×240 LCD, 16-bit color, 50-line single DMA draw buffer (32,000 bytes), 16 MB flash and observed 16 MB PSRAM.
- Logical version: LAB-03/v2 = scripted firmware `da05fed2c2c95a8876189946` + server LAB-03/v1 + fixture `lab03-media/v1`.
- Manual firmware: `0930a45c81547a896c3a33b2`. An isolated fresh manual build produced the same compiled-input ID and SDK-config hash. Binary hashes differ by build artifacts/timestamps and are preserved separately.
- ESP-IDF 5.4.2; BSP 3.2.1; codec 1.5.11; LVGL port 2.9.0; LVGL 9.6.0~1. Wi-Fi power saving is disabled in the candidate. Connect deadline 3s; header/body reads 500ms; overall fetch deadline 15s. EAGAIN retains bytes in the same response.
- Fixture manifest SHA-256: `e5a2bca61e22453f9cddea26fc2f0167a3de257510a45d780b7fc24c6282f82c`. Server source SHA-256: `afc7da6d5b68dd60daba3eef29a72093775294983ab8cce18bfa41b57d0ea2c6`.
- Commands: `make lab.lab03.scripted KIT=lynn RUN_ID=20261017-lab03-scripted-06`; `make lab.lab03.build KIT=lynn`; fresh manual verification used a temporary registry copy with a new build directory. Default scripted capture is 780 seconds, ending after validated harness completion plus worker drain.

Full fingerprints, binary/config hashes and dependency-lock hash: [build verification](build-verification.json). Host registry/schema validation, help and make dispatch passed. Final full host test result: **241 tests passed**. Native tests verify bounded dense replay, order preservation, malformed input and pause behavior; host tests cover fixed fixture regeneration, HTTP identity/failure, parser honesty, cancellation, fresh-header restoration and pre-reset trace exclusion.

## Scope and foundation

LAB-02's bounded inbox model is compiled directly and unchanged. Its session-coupled card views cannot be imported cleanly without bringing a second session owner, so LAB-03 adapts its retained-card pattern to three persistent roots with one media image. The Wi-Fi lifecycle is copied into a LAB-03 namespace. No LAB-02 source, product firmware, shared client code or archive source was changed. Nothing is promoted for product use.

The candidate uses bounded whole-object PCM/sketch prefetch, one media worker, one page worker, a bounded command queue and nonblocking UI publication. It is a lab control UI; it does not evaluate swipe/snap animation or approve product interaction design. Focus changes recycle presentation bindings and hide the active media canvas; physical offscreen movement and scanout remain unconfirmed without the recording.

## Scenarios and operation proof

- `play_pause_resume`: observed in target trace.
- `focus_B_play_B`: observed in target trace.
- `rapid_20_A_B_A`: observed in target trace.
- `cancel_during_audio_fetch`: observed in target trace.
- `cancel_during_sketch_fetch`: observed in target trace.
- `new_page_during_playback`: observed in target trace.
- `signout_during_operation`: observed in target trace.
- `asset_failure`: observed in target trace.
- `audio_only`: observed in target trace.
- `maximum_dense_sketch`: observed in target trace.
- `pause_then_move_offscreen`: observed in target trace.
- `sustained_600s`: observed in target trace.

The rapid path contains 20 accepted A/B/A triplets with worker terminals. The sustained phase started 163 operations: 162 completed, 1 cancelled at the final stop boundary, 0 failed. Across the capture, 232 accepted operations produced 232 terminal events; exactly-one check = `True`. Stale results rejected: 67. Detected heap allocation failures: 0; wrapper allocation attempts: 8.

The parser checks actual stage ordering rather than scheduled intent alone. First wall-time candidates missed cancellation stages and completed dense replay; the stage-driven harness corrected that. A later attempt exposed a temporary-read error, followed by a connect timeout. These failed/aborted runs remain under `runs/20261017-lab03-scripted-01` through `runs/20261017-lab03-scripted-05`; they are not passing results. The fixed fixture bytes, hashes, delays and D failure behavior stayed unchanged.

## Measured distributions

All durations are milliseconds. Codec submission and UI invalidation are explicitly **proxies**, not first audible sample or confirmed LCD effect. Decode here is sketch hash/validation and timed-record decode; audio input is PCM, without a compressed decoder.

| Measurement | p50 | p95 | p99 | Worst |
|---|---:|---:|---:|---:|
| UI callback | 0.145 | 2.331 | 3.222 | 11.413 |
| Raster update | 0.036 | 0.446 | 0.832 | 2.485 |
| Sketch validation/decode | 4.966 | 5.950 | 6.303 | 6.829 |
| Cancel → UI property effect | 3.986 | 7.303 | 7.726 | 8.285 |
| Cancel → worker terminal | 13.137 | 204.559 | 312.975 | 312.975 |
| Start → codec write | 609.171 | 1372.354 | 1835.693 | 2126.662 |
| Start → first UI frame | 632.692 | 1380.610 | 1851.430 | 2150.854 |
| Start → audible sample | missing | missing | missing | missing |
| Stroke reveal, continuous focus proxy | 59.000 | 114.000 | 118.000 | 194.000 |
| Stroke reveal after focus returns proxy | 269.000 | 513.000 | 535.000 | 539.000 |
| All stroke reveal observations proxy | 60.000 | 114.000 | 119.000 | 539.000 |
| Pause request → worker effect | 3.261 | 21.534 | 21.534 | 21.534 |
| Pause request → UI effect | 32.104 | 32.969 | 32.969 | 32.969 |
| Resume request → worker effect | 11.090 | 11.150 | 11.150 | 11.150 |
| Resume request → UI effect | 31.586 | 31.607 | 31.607 | 31.607 |

Maximum raster work: 31 segments/update, against a declared cap of 64. Maximum due-but-unrendered backlog: 0. Worker busy-time distributions and every UI/audio-position/render-progress observation are retained in metrics. Busy time includes blocking codec writes and must not be called CPU utilization. Network buffering intervals are separate; actual audio underrun count/duration and renderer attribution are **unknown**. All starts refetch media; no warm-cache result is claimed.

Reveal samples use each newly presented stroke's due time; sparse gaps create no samples. Returning from hidden presentation is reported separately. Continuous-focus p95 is 114ms, exceeding the provisional 100ms exploration target. Zero renderer backlog and short callbacks suggest reviewing the 100ms publication cadence plus UI sampling before blaming raster saturation; this is an inference requiring DAC/LCD timing. No synchronization pass is claimed.

## Memory, stack and sustained drift

| Heap class | Sampled used peak | Conservative used upper bound from regional minima | Smallest sampled largest-free block |
|---|---:|---:|---:|
| Internal | 201044 B | 205344 B | 31744 B |
| PSRAM | 314212 B | 314348 B | 16252928 B |
| LVGL pool | 11420 B | pool high-water | 52440 B |

Media worker minimum remaining stack: 5576 B of 8,192 B; observed used high-water 2616 B. UI minimum remaining stack: 2676 B of 7,168 B; observed used high-water 4492 B. These are sampled stack high-water observations, not approved reserves.

One logical active/paused image is retained using **three 28,800-byte RGB565 copies** (worker raster, publication, display): 86,400 B resident. Audio scratch is 96,000 B; sketch scratch is 40,968 B; media cache/preview bytes are zero. The 32,000-byte LCD draw buffer is additional and included in aggregate heap observations. Codec/DMA copies are not directly counted. No replay-to-position reconstruction occurs on resume because the raster is retained. Heap-failure callback and wrapper counters do not independently instrument LVGL pool exhaustion.

Declared next allocation plus reserve: internal 8,192 + 16,384 = 24,576 B; PSRAM 96,000 + 32,768 = 128,768 B. Compare these against the measured largest-free blocks above; sums of regional low-water marks are conservative bounds, not simultaneous peaks.

Matched completed-state `internal_heap`: free 109511 → 109531 B (delta +20 B); range 109511–109539 B.

Matched completed-state `psram_heap`: free 16463004 → 16463004 B (delta +0 B); range 16463004–16463004 B.

No sustained heap/cache growth was observed in this ten-minute window. This does not establish a lifetime bound or test a growing cache; the lab has no media cache.

## Evidence, limitations and follow-up

- [Run manifest](runs/20261017-lab03-scripted-06/run-manifest.json), [metrics v2](runs/20261017-lab03-scripted-06/metrics.json), [events](runs/20261017-lab03-scripted-06/events.jsonl), [analysis and artifact hashes](runs/20261017-lab03-scripted-06/analysis.json), [server correlation](runs/20261017-lab03-scripted-06/server-correlation.json).
- Full serial and compressed per-stroke observations are in `logs/device/labs/lab-03/20261017-lab03-scripted-06/`; analysis records hashes and byte counts. Each stroke retains UI invalidation time, submitted PCM position, fixture due time, proxy lateness and focus context.
- [Synchronized capture status](runs/20261017-lab03-scripted-06/synchronized-capture.json): **UNAVAILABLE**, with the [device inventory](capture-device-inventory.txt). No video/audio was synthesized. Configure an external camera/microphone with `tools/capture_av.py` and record an explicit sync cue on the next run.
- [Independent read-only review](independent-verification.md). Build/model coverage is L0/L1; real target traces establish execution and operation ordering. Acoustic and LCD-visible behavior remain unconfirmed.
- Run06 retains an older adapter's default `capture.status` metadata even though its activity stage, fresh-ready event and harness terminal prove capture. Later adapter bookkeeping sets the observed capture status and startup level; [manual dispatch run08](runs/20261017-lab03-manual-dispatch-08/run-manifest.json) verifies that correction (workflow PASS, captured status, fresh-ready true). Historical evidence is preserved rather than rewritten.

- The fixed v1 fixture uses red/green/blue strokes and does **not** cover the common method's background-colored erase strokes. Preserve its comparison bytes; add that case in a new fixture revision and rerun the affected paths.
- Immediate restart run07 hit TCP `TIME_WAIT` / address-in-use. Run08 passed after cooldown. The host does not reuse or stop a pre-existing listener; allow the port to cool before immediate repeated runs. This operational limitation remains for runner follow-up.

Next: obtain synchronized screen/speaker evidence, add DAC/DMA underrun counters and accurate audio position, measure actual paint/visible effects and CPU utilization, review publication cadence, add versioned background-colored strokes, and compare bounded streaming versus full prefetch if startup remains limiting. Any move into shared client code or product firmware requires a separate review.
