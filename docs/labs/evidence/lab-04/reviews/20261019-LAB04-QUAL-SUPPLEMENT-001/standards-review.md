# LAB-04 QUAL-SUPPLEMENT standards review

## Scope and method

Read-only review of the LAB-04 qualification report and evidence-index change in run `20261019-LAB04-QUAL-SUPPLEMENT-001`, using the current uncommitted LAB-04 source state. Reviewed the full client application coding standard, `docs/AGENTS.md`, LAB-04 sections 2 and 3.1 and execution sections 8.1–8.6 of the frozen implementation map, the sketch experience specification, and the LVGL budget foundation. This records the implementer's standards inspection; independent read-only verification is a separate required QUAL gate and is not claimed here.

The implementation map remained read-only: `docs/labs/remaining-labs-implementation-plan.md`, SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`.

## Applicable sources

| Source | Revision / authority | Relevant requirements | Applied to |
|---|---|---|---|
| `docs/standards/client-application-coding-standards.md` | Normative; SHA-256 `3dbd29747c44dee6400ce06f2f44664a09e92fc4cbffcd8aec003610eaf2e5d9` | §§2–4 bounded callbacks, single-writer state, async terminal outcomes; §§7–8 operational contract and resource evidence; §10 observability and truthful loss reporting | Report interpretation of firmware queue loss, missing capture evidence, terminal outcomes, and point resource samples |
| `docs/labs/remaining-labs-implementation-plan.md` | Frozen implementation map; read-only, SHA above | §§2, 3.1, 8.4–8.6; S-01–S-22; L04-01–L04-11; L04 package scope and evidence gates | Acceptance ledger, level boundaries, exact pending target criteria |
| `docs/product/experiences/sketch-record-and-playback.md` | Product experience source; SHA-256 `87efc274b61114887bfa21f44ba8e47b23e710dbac7f2481d28687b25e63a2be` | SK-002/004/007 and EXP-SKP-001–004; OPEN-SKP-01 and OPEN-SKP-03 | Keep logical size and decided experiment separate from open wire/color/legacy choices |
| `docs/product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md` | Product device UI foundation; SHA-256 `20bdeb5883349e00bbe1fb649f3a2477df91e313cddebf8629ab39ad6164e987` | Ledger A/B resource definitions; no approved performance threshold | Distinguish actual resource point sample from simultaneous peak and avoid performance PASS |
| `docs/product/communication-contracts/message-delivery.md` | Communication decision source; SHA-256 `7ccf959daa5bbf9c711cd71eb2f8e2cf3585a97e9665fa1c022f03060d39edc2`; COM-OPEN-002 remains OPEN | No decided delivery/confirmation rule | Preserve COM-OPEN-002 as OPEN; no product inference |

## Findings

### QUAL-SUP-001 — stale snapshot framing after the HOST/FW evidence contract changed

- **Classification:** confirmed report defect, in scope.
- **Trigger/effect:** the preceding report started with a present-tense current status and current-source/build table. FW later added supplemental point-resource and publication-queue events; HOST later learned to parse them and bind future run identity. The two preserved successful captures contain 35 scenario rows each but contain neither supplemental event and have null source/build/fixture identity. Without a dated boundary, readers could mistake the previous parser snapshot for evidence about the present telemetry contract.
- **Evidence:** current-parser outputs for manual `20261019-manual-run-243e8112` and scripted `20261019-scripted-run-4587ca24` both report `device_resource_samples=MISSING`, `firmware_publication_queue=UNKNOWN`, `firmware_trace_loss=UNKNOWN`, `trace_loss=UNKNOWN`, and `evidence.status=INCOMPLETE`. Both preserve the decided experiment verdict `PASS`; this is separate from evidence completeness, current-source attribution, and performance.
- **Fix:** the report now leads with the current status, describes the current parser/adapter behavior, records the captures as pre-current-source/unbound, distinguishes firmware queue rejections from end-to-end trace loss, and labels the former report body a historical snapshot. The evidence index now repeats the current qualified/pending boundary. No run or source was modified.
- **Check:** current-parser post-hoc derivatives in this directory; source and raw-capture hashes in the qualification report; focused package, registry/help, build, and root-suite checks recorded in the final QUAL verifier result.

## Non-findings and residual evidence limits

- The parser's `INCOMPLETE` evidence envelope is correct for these older captures; it is not a defect to preserve their decided workflow/experiment `PASS` values separately.
- A firmware publication-queue rejection counter does not measure UART transmission, capture, or host parsing. A future capture still needs a trace-loss reconciliation across those boundaries.
- Resource readings in scenario rows and any future `resource_sample` are point observations. They do not satisfy L04-07's simultaneous decode-plus-live-image peak without phase-synchronized evidence.
- Performance thresholds, per-color RGB565 mapping, legacy policy, seek/checkpoint/cache policy, and communication decision `COM-OPEN-002` remain OPEN. This review does not close them.
- Current-source display, touch, manual completion, and supplemental telemetry behavior lack a new target capture. This is pending device evidence, not grounds to invent a software PASS or a source fix.

No additional confirmed report defect was found within the reviewed current QUAL scope. The independent verifier should confirm this assessment, the report/index claims and hashes, all acceptance dispositions, and preservation of raw evidence.
