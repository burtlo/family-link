# LAB-04 qualification evidence recut — 20261019-LAB04-QUAL-DEVICE-RECUT-001

## Scope and identity

This derived QUAL review covers the two supplied runs `20261019-manual-run-243e8112` and `20261019-scripted-run-4587ca24`. The original manifests, event journals, metrics, and serial logs were read only. Current parser output is saved beside this note. L04-DEVICE remains unselected; this review is not an independent DEVICE verification or an accepted L2/L3 DEVICE verdict.

The controlling implementation map is `docs/labs/remaining-labs-implementation-plan.md`, SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`. Current LAB-04 source fingerprints were independently recomputed: `main/app_main.cpp` `1bd2df8e6fd91035a193dfd2f15403cdb57fa0b54598801dc6b53074dbd5296f`; `main/preview_accounting.hpp` `72b19ed12dac01d747fbe960ba2ca3493a987ac008f566150002ed146cf5f91f`; parser `9ecc6ae260da55d5a2cada37c124914cac4774fd3bf03494998bbae0381c2558`; trace schema `125c94daafaee3ca2f5c2180797a896fbf1a82c761e5e3cebd2413ef0638e7dc`; adapter `8d68f917fc8fb78b4464d9777d8aa8b92115bb2328dae967a186d03765039b73`; host test `a6ea1d4b557ffde56e54ed46c169953c243279cbb9e9dae52e00a8ef8f0b6719`; model test `af46ea2a471730c229cae474df3330e9319f2a1ff31bac34c97f5c1bfd1fea15`; project README `f44bca4a1bb820a4bffc80f9eddb8de8d61767c0a9f65ddcfae210ce9399e307`.

Current registered build outputs were also hashed independently: manual `build/labs/lab-04-sketch-format-manual/lab04_sketch_format.bin` `1f2712bbf2c35a96eea3cd4bf5ba4b52165f8f8fb7c44b2edb115e7a41058fb4`, sdkconfig `c801c2f8544ef8540f8e09f74a8499c41465dd622e32110eeed299aa60f2aa3e`; scripted binary `2146d44e60130d2b2ff02a0b9af6efe2a22402bfdc44d515b4720bb4b7522815`, sdkconfig `0c37fd773cf173eb21b996f35c37cf78ba89f505b9a2ddea0058d5fed17a40a4`. These describe current build outputs only. Neither run manifest binds these artifacts: firmware source/build/build ID/IDF/sdkconfig and fixture identity fields are null, and `source.files` is empty. No run-to-current-source/build identity is inferred.

The fixture still has the existing project identity `lab04-sketch-format/v1`, Corpus A `short.sketch` 128 bytes / SHA-256 `d2efa7e48bcb2eb021211d208acd231a05919edfb00d783dd9221720771d12ec` and `dense.sketch` 40,968 bytes / SHA-256 `1bac193a2482c8a4f743d1bf6ada17080c87ca2d58f7964fb20d481664c50985`. The run manifests themselves leave fixture fields null.

## Raw evidence and parser reproduction

Both manifests report `capture.status=captured`, `post_reset=true`, parser `parsed`, cleanup `passed`, server/scenario `not_configured`, `workflow_status=PASS`, and `experiment_verdict=PASS`. Each has a complete `LAB04_HARNESS_DONE status=complete variants=3 checkpoints=0` marker, 35 serial trace rows, and the exact decided set `F-malformed`, `F-overflow`, `F-cancel`. The manual interval is 2026-10-19 19:03:36.113113Z–19:04:30.806585Z; scripted is 19:04:51.320675Z–19:05:02.846521Z. These are workflow and functional experiment outcomes; they do not assert performance PASS.

Original artifact SHA-256 values:

| Run | Manifest | Events | Original metrics | Serial capture |
|---|---|---|---|---|
| Manual | `ec06988f75640c49dbfa84de59177aa44fb7e824e6298d212113fe1144f902ea` | `f1f30e3340825270f087cc4941b702bde1ec2020e70b2ccbbe2779f47b2e1ae2` | `8e37eb803660f465c971ba26ec003e22a276003d71b1f4f1a06c08df766ee91b` | `13690751337046bba4e564552b6c09d9f8a1579ec5ec5a4772975fce5c15595a` |
| Scripted | `201141a388e99d7466a1cfcb4c03303145b53807b3e3c052992534486284d772` | `3c8f12261b2034df095db42b5eaff57814b173866703763b8db74c7348076d72` | `443cd66c3cd1d2378f99fa656de51ce97ccb4118b99c295c90e6d633a8d57f9e` | `13898a71d02448d42cb348ffe0cde91e708a6697f88b04e7026888a0ea886e35` |

Current-parser reparse command from the repository root (run once for each listed `(mode, run_id)` pair):

```sh
.venv/bin/python - <<'PY'
import importlib.util
from pathlib import Path
root = Path.cwd()
spec = importlib.util.spec_from_file_location(
    "lab04_parser", root / "client/labs/lab-04-sketch-format/tools/parse_lab_trace.py")
parser = importlib.util.module_from_spec(spec)
spec.loader.exec_module(parser)
out = root / "docs/labs/evidence/lab-04/reviews/20261019-LAB04-QUAL-DEVICE-RECUT-001"
for mode, run_id in (("manual", "20261019-manual-run-243e8112"),
                     ("scripted", "20261019-scripted-run-4587ca24")):
    parser.parse_file(
        root / "logs/device/labs/lab-04" / run_id / "serial.txt",
        out / f"{run_id}-posthoc-metrics.json",
        expected_run_id=run_id, expected_mode=mode, expected_variant=mode,
        post_reset=True,
        capture_context={"kind": "device_serial_capture", "target_id": "lynn-kit",
                         "post_reset": True})
PY
```

The generated files are [manual post-hoc metrics](20261019-manual-run-243e8112-posthoc-metrics.json) and [scripted post-hoc metrics](20261019-scripted-run-4587ca24-posthoc-metrics.json). Both independently parse 35 rows and reproduce experiment `PASS` for the exact decided inventory. Parser provenance is `hardware` only because the caller supplied the manifest's explicit kit nickname and post-reset capture context; evidence envelope remains `INCOMPLETE`, with `trace_loss=UNKNOWN`, `firmware_trace_loss=UNKNOWN`, and missing `firmware_trace_loss`. The manifest's zero host journal drops do not prove zero firmware trace loss.

## C-1/C-3 comparison and measured observations

The prior QUAL refresh identified C-1 preview-accounting and C-3 raster-counter gaps in the older runs. In these new captures, the parser observes the fields on all 35/35 rows. For each run, `preview_buffers` is 1 and `preview_bytes` is 28,800 on eight rows, and both are zero on the other 27 rows; the preview field pair is internally consistent and within the declared one-image storage ceiling. All 35 rows contain `raster_work_cap=64`, `raster_eligible`, `raster_rendered`, and `raster_remaining`. Eight rows have eligible raster work: eligible sum 8,270, rendered sum 206, remaining sum 8,064; each row respects the cap and `remaining = eligible - rendered`. Thus the fields absent from the old captures are present and structurally coherent here. Since the run's source/build identity is unbound, these observations do not prove that the corrected current source/binary produced them; C-1/C-3 source attribution remains open for a source-bound DEVICE run.

Each run has real timing series, parsed with hardware provenance from the explicit capture context. Values below are p50 / p95 / p99 / worst; units for the first four are microseconds. These are descriptive observations, not approved thresholds.

| Measure | Manual (n) | Scripted (n) |
|---|---:|---:|
| `encode_us` | 904 / 25,000 / 25,000 / 25,000 (9) | 375 / 20,363 / 20,363 / 20,363 (9) |
| `decode_us` | 293 / 18,054 / 18,054 / 18,054 (8) | 90 / 15,384 / 15,384 / 15,384 (8) |
| `raster_us` (eligible work only) | 122 / 3,818 / 3,818 / 3,818 (8) | 70 / 1,851 / 1,851 / 1,851 (8) |
| `seek_us` | 6 / 86 / 86 / 86 (9) | 6 / 41 / 41 / 41 (9) |
| `internal_largest_free_block_bytes` | 139,264 / 139,264 / 139,264 / 139,264 (9) | 139,264 / 139,264 / 139,264 / 139,264 (9) |
| `psram_largest_free_block_bytes` | 16,515,072 / 16,515,072 / 16,515,072 / 16,515,072 (9) | 16,515,072 / 16,515,072 / 16,515,072 / 16,515,072 (9) |
| `lvgl_largest_free_block_bytes` | 53,416 / 53,416 / 53,416 / 53,416 (9) | 54,152 / 54,152 / 54,152 / 54,152 (9) |

L04-06's requested timing fields and raster eligibility are now represented by raw trace and post-hoc metrics. L04-07's three heap series are also present. However, the evidence envelope is incomplete, the build/source is unbound, and there is no synchronized independent ledger demonstrating that the three heap readings are at the specific simultaneous decode-plus-live-image peak required by L04-07. Trace rows report `live_buffers=0` throughout; the separately emitted `preview_buffers=1`/28,800-byte observations do not establish simultaneous live preview and decode occupancy. Treat these as candidate measurements for DEVICE verification, not completed L3 acceptance. Both post-hoc metrics correctly retain `performance_verdict=UNMEASURED`; the plan has no approved performance budget.

## Qualification dispositions and open items

- L0/L1 software qualification remains supported by the current registered builds and existing focused/full-suite evidence recorded in the report; this evidence recut did not rerun those checks.
- L2 workflow and experiment observations are PASS for both modes as reported by manifests and independently reproduced from raw serial with the current parser. Exact decided inventory is complete; exploratory observations remain excluded from verdict inputs.
- New captures close the prior *field-presence* gap for C-1/C-3 at the trace level, but not their attribution to the current source/build. The source/build identity fields are null.
- Raw L04-06 timing and L04-07 heap samples exist. The QUAL package records and reconciles them, but L04-DEVICE is unselected and no accepted L3/DEVICE verdict is made. Performance remains `UNMEASURED` because no owner-approved budgets exist.
- L04-07's required simultaneous decode-plus-live-image peak is not established; heap samples cannot be promoted to that claim from the supplied trace.
- Keep `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` open. No format, color-wire, checkpoint, legacy, cache, seek, or performance policy is decided here.
- Existing LAB04-F001–F009 standards findings and prior regression coverage remain as recorded in the report. Allocation-failure terminal behavior remains an unproven residual risk without fault injection or source-bound device evidence.

No original run artifact was altered. No selected-package scope, product decision, source, test, or device state was changed.
