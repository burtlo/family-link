# LAB-04 client standards review and QUAL disposition — `20261019-LAB04-TRACE-PEAK-001`

## Scope, authority, and provenance

This is the durable record for the fresh read-only client standards review and the subsequent L04-QUAL report/index refresh. The review examined the selected LAB-04 client implementation, parser, trace schema, tests, and evidence claims. It did not review or modify other labs, `client/device`, the implementation map, product standards/specifications, or raw run evidence. The manual on-screen guide was sufficient; no guide defect was found. L04-DEVICE was not selected. No flash, target interaction, or current-source runtime capture was performed.

Applicable authorities:

| Source | Role and relevant requirements |
|---|---|
| `docs/standards/client-application-coding-standards.md`, SHA-256 `3dbd29747c44dee6400ce06f2f44664a09e92fc4cbffcd8aec003610eaf2e5d9` | Normative. §8 requires allocation failure to produce a terminal operation outcome and defined user-visible behavior; §§4 and 11 govern operation envelopes and evidence levels; §11 requires an explicit target/runtime limitation when a device-specific finding is called fixed below L2. |
| `docs/labs/remaining-labs-implementation-plan.md`, SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b` | Read-only implementation map. §§2, 3.1, 8.4 and 8.5 define L04-QUAL scope, allowance, and L04-07 / evidence boundaries. |
| LAB-04 trace schema and model/host contracts | The `resource_peak` row uses `expected_output_segments` and `ledger_b_expected_output_segments`; it describes maximum decoder capacity reserved before any record insertion, with zero decoded segments at observation. It is not a whole-system high-water measurement. |

The fresh reviewer recorded source provenance in the orchestration log `docs/labs/evidence/lab-04/orchestration-progress.md` under this run. Current LAB-04 project source-tree SHA-256, computed with the adapter's `_tree_sha256(..., fixtures=False)` path framing and exclusions, is `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549`. Current fixture-tree SHA-256 is `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`. Key current file hashes:

| Path | SHA-256 |
|---|---|
| `client/labs/lab-04-sketch-format/components/lab04_format/include/model.hpp` | `fda886305eacdd17ffbe800275e50b4f2e0febec673d3e2355c644be8c24c7f2` |
| `client/labs/lab-04-sketch-format/components/lab04_format/model.cpp` | `510f0b5c2a35afb55889ee0ad313082e5c719a9c9d578a31a51006b49e1d0605` |
| `client/labs/lab-04-sketch-format/main/app_main.cpp` | `d25b8002874923c484804adf7d206e6f6dd427cbfc794c6bcc267b4bec90b60c` |
| `client/labs/lab-04-sketch-format/tools/parse_lab_trace.py` | `2674bc20b97b8f0528cfbc3f742de86a90c5c8b0e9482b563a29ebf6e0fb0d41` |
| `client/labs/lab-04-sketch-format/trace-schema.json` | `18c9fa0e52e337a6d6acf9224c795996b885ed055d28b146738c1a97b16acb08` |
| `scripts/tests/test_lab04_model.py` | `049fb4f23b9e5bff7b144c260ef884a5414d22886ec9e4f9e737aa27d32084ce` |
| `scripts/tests/test_lab04_host.py` | `2ca074326bf3c2a448a72537d7d1c898a2e46e0f343a4f21b26a1eeba6f1faf0` |

Prior user captures are preserved, including manifests, event journals, metrics, serial logs, and the prior raw-hash ledger. Those reset-gated captures bind to source SHA-256 `1ffa7898a10582933438339e112ccab2fb58e4304cc0be323ad3bc6d5a40c1b4`, not current source `93fa2323…c549`. Their workflow/experiment PASS outcomes and raw hashes remain historical and are not rewritten, discarded, or used as current-source L2 evidence. Current source has L0/L1 evidence only; there is no current-source target run.

## Review findings and per-ID outcomes

### `LAB04-F001` — P2 confirmed: peak rows could be surfaced without a unique successful terminal binding

**Finding.** The host parser could accept a `resource_peak` observation without proving it belonged to exactly one successful preview operation. An orphan or duplicate peak, a peak paired with a failed operation, or a peak with the wrong fixture/variant could therefore be represented as an observation without a unique valid terminal owner. That weakens trace integrity and could make capacity-reservation evidence misleading.

**Fix and outcome.** The parser now binds each peak to exactly one successful `F-preview` terminal and requires matching operation ID, fixture, and variant. It rejects orphan, duplicate, failed-terminal, and wrong-variant/identity peaks. Peak field names remain `expected_output_segments` and `ledger_b_expected_output_segments`; sampling still occurs after maximum-capacity reservation and before record insertion, when decoded segment count is zero. No whole-system peak or L04-07 claim was added. A fresh independent verifier `lab04_peak_terminal_verify` passed all 28 HOST tests and whitespace checks. **Outcome: fixed and independently verified at L1.**

### `LAB04-F002` — P2 confirmed: allocator failures lacked operation terminal and user-visible failure

**Finding.** `candidate.segments.reserve(kMaximumSegments)` in the production decoder, encoder vectors, and other standard containers/strings can allocate. The direct manual and scripted ESP-IDF sdkconfig builds had C++ exceptions disabled, so an allocation failure could abort/escape an accepted model or worker operation without exactly one failure terminal or a visible failed/stopped harness result. The existing semantic failure paths did not cover `std::bad_alloc`.

**Fix and outcome.** The LAB-04 manual and scripted defaults now enable `CONFIG_COMPILER_CXX_EXCEPTIONS=y`. Public model encode/decode paths, `DecodeOperation`, and `EncodeQueue::push` convert `std::bad_alloc` to the short `no_memory` failure reason; the reason is recorded using the target string's inline storage. `DecodeOperation` terminal state commits once, transfers the reason by move, and clears live-buffer count. The worker has a fixed-buffer fallback failure event for bad allocation outside model guards; the app's existing `observe_completion` marks the harness failure, drains/stops the worker, reports `Stopped - reset to rerun`, and emits `LAB04_HARNESS_DONE status=failed` rather than a success marker. Test-only global `operator new` fault injection exercises the production preview reservation and encode allocation points. It asserts the failed `DecodeOperation` has one failure terminal, zero live buffers, `no_memory`, and preserves the destination; encode returns a failed result with empty bytes and `no_memory`.

The fresh independent verifier `lab04_alloc_failure_verify` passed 11 model tests, 28 HOST tests, and fresh manual/scripted ESP-IDF v5.4 builds with exceptions and `-fexceptions` effective. This review's final package matrix repeats those checks. **Outcome: software mechanism fixed and independently verified at L1; device-specific behavior remains unverified pending a new current-source scripted target run.** In accordance with coding standard §11, this finding is not called fixed at L2 or a device PASS. This is not an approved waiver.

## Non-findings, open decisions, and evidence limits

- The manual on-screen guide is sufficient; no change or product decision is required.
- The parser's peak observation is limited to decoder reserved capacity and the identified allocation ledgers at the specific reservation point. Zero segments are inserted at the observation. It is not a whole-system high-water and does not satisfy L04-07.
- `L04-07` remains not achieved until target memory readings and their simultaneous-lifecycle conditions are verified. The old captures' point samples are bound to the old source.
- `L04-05`, `L04-06`, trace sequence/footer and end-to-end loss reconciliation, out-of-memory worker runtime behavior, and serial/capture path require a new target capture of the current source. Existing capture PASS states are historical and distinct: they report workflow and experiment outcomes for the earlier source, not current-source behavior.
- Performance is `UNMEASURED`; no owner-approved budgets exist. L04-DEVICE remains unselected.
- `OPEN-SKP-01`, `OPEN-SKP-03`, `COM-OPEN-002`, and related format, palette/wire, seek, checkpoint, legacy, and cache choices remain OPEN.

## Execution plan and package boundaries

The sequential plan was MODEL → FW → HOST → QUAL. This review was read-only until implementation reports from F001/F002 and fresh L0/L1 verification were available. QUAL refreshed only the LAB-04 report, the LAB-04 evidence-index row, and this review artifact directory. Existing report history and raw evidence remain preserved. The implementation map's L04-QUAL package allowance was used for the single F002 repair; no further source repair is authorized in this report-only phase. The independent QUAL verifier is the next step.

## Final qualification command record

Commands and results below are actual runs for `20261019-LAB04-TRACE-PEAK-001`; no target operation is included. The root suite runs only after this report content is stable.

| Command | Result |
|---|---|
| `.venv/bin/python -m unittest scripts.tests.test_lab04_host scripts.tests.test_lab04_model -v` | **PASS**, 39 tests: 28 HOST and 11 MODEL. |
| `.venv/bin/python scripts/lab_registry.py validate` | **PASS**, registry valid. |
| `make config.labs` | **PASS**, LAB registry snapshot generated. |
| `make lab.help` | **PASS**, LAB-04 manual/scripted entries listed. |
| `make lab.lab04.help` | **PASS**, operator guidance and unsupported backend printed. |
| `.venv/bin/python` Draft 2020-12 schema check using `Draft202012Validator.check_schema` on `trace-schema.json` | **PASS**, schema valid under Draft 2020-12. |
| `make lab.lab04.build` | **PASS**, registered manual build; binary size `0xBDC60`. The first sandboxed attempt was blocked by macOS `psutil` process enumeration; authorized retry passed. |
| `make lab.lab04.scripted.build` | **PASS**, registered scripted build; binary size `0xBDD10`. |
| `.venv/bin/python -m unittest discover -s scripts/tests -v` | Sandbox-first run: 318 tests with 15 unrelated LAB-02/LAB-03 localhost-bind `PermissionError`s. Narrow loopback-permission retry: **PASS**, 321 tests in 100.964 s. |
| `git diff --check` | **PASS**, no whitespace errors after report/index/review refresh. A separate trailing-whitespace scan of all three documentation files also passed. |

Direct isolated implementation builds also passed before report refresh at `/tmp/lab04-qual-manual` and `/tmp/lab04-qual-scripted`. Both registered builds and both isolated builds have effective `CONFIG_COMPILER_CXX_EXCEPTIONS=y`, and their generated `build.ninja` compile flags contain `-fexceptions`. Registered current build identities recomputed with the adapter's identity algorithm:

| Variant | Source SHA-256 | Fixture SHA-256 | sdkconfig SHA-256 | Binary SHA-256 | Build fingerprint |
|---|---|---|---|---|---|
| Manual | `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549` | `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160` | `6f817f57e4ba43cd2605206ca1acfd3656e8fc820b3587df86b7fb54f896bb91` | `3715384a17bc207a8057267cd2a2d9bbb2ecde3c01f5023ce74d0ac917a55009` | `sha256:010d99b2388e57774da8a1f61ddcf54fe7803a6607afde0fdf969fb9ea037b14` |
| Scripted | `93fa23239c350d7367c91c224e56c917c3bd0137dc9d8643d2c52c4fda8cc549` | `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160` | `96f04388192ddd8ade0394ae45aa6a4f45a559e08e93f487933aef18e3aff184` | `09df28b9bf6d5a27bc34449d0e1800c590d6f6a4b2461d4b66c7a69bd1bf845e` | `sha256:6afd68552c5b48f798ee1d4a169982c41c0c4678bd7ac0d2ecef705a8be7efe4` |

These current software build identities are distinct from the preserved user-run identities: the older manual/scripted manifests name source `1ffa7898…c1b4`, firmware binaries `b3c24f86…9746` / `43090093…52b6`, sdkconfigs `c801c2f8…aa3e` / `0c37fd77…40a4`, and build fingerprints `sha256:5aa83c9d…5052` / `sha256:9dfa7e41…b79e`. This establishes that the preserved captures are not current-source runs. No current-source target-run identity exists. The new builds were not flashed or run.
