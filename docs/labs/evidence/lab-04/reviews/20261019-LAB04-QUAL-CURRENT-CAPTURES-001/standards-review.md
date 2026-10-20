# LAB-04 QUAL current-captures standards review

## Scope and provenance

Read-only review of the LAB-04 qualification summary and evidence-index row for `20261019-LAB04-QUAL-CURRENT-CAPTURES-001`. The review considered the current uncommitted source tree, the two newly supplied current-source hardware captures, the previous `QUAL-SUPPLEMENT-001` review finding, and their immutable raw artifacts. No LAB-04 firmware or host source was edited in this refresh. The review is intentionally evidence-facing: the two target runs are user-provided and this agent did not operate or flash the device.

The controlling map stayed read-only at SHA-256 `5e74fece366d4b16a0519302a7d8e9a082a6332e02bdd4eed37f40291bf5222b`. Repo HEAD is `a101f504907fc27a8862f321e1502c789f5f5c23`; current LAB-04 source tree SHA is `1ffa7898a10582933438339e112ccab2fb58e4304cc0be323ad3bc6d5a40c1b4`; fixture tree SHA is `05a0f684d13831ed47046ec78cfd4c76116b1c70a59d7da5c119e37de81f3160`. The per-run source/build identities were recomputed against current project and build artifacts and match their manifests and serial READY markers. Raw run artifact hashes are captured in the adjacent `raw-artifact-hashes.json`, including the preserved pre-current-source runs referenced by QUAL-SUPPLEMENT-001.

## Applicable sources

| Source | Revision, authority, and fingerprint | Relevant requirement | Applied to |
|---|---|---|---|
| `docs/AGENTS.md` | Active repo guidance; SHA-256 `e663f9914fd945905c9e463f04ce31b1a6b18ae88dc4e40b7de5f34ed52bf5c0` | Active product sources govern; POC archive is read-only; evidence belongs under `docs/labs/evidence/` | Source selection, scope and artifact placement |
| `docs/standards/client-application-coding-standards.md` | Normative client standard; SHA-256 `3dbd29747c44dee6400ce06f2f44664a09e92fc4cbffcd8aec003610eaf2e5d9` | §§2–5, 7–8, 10–15: bounded work, outcome truth, OPEN behavior, measured budgets, telemetry, verification-level claim | Capture interpretation, trace loss, budgets, device/software boundaries |
| `docs/labs/remaining-labs-implementation-plan.md` | Read-only execution map; SHA above | §§2, 3.1, 8.1–8.5: fixed decided input inventory, current evidence and all L04 criteria; L04-06 percentile reporting; L04-07 simultaneous peak | Acceptance ledger and pending criteria |
| `docs/product/experiences/sketch-record-and-playback.md` | Rev 3, OWNER-DECIDED intent; SHA-256 `87efc274b61114887bfa21f44ba8e47b23e710dbac7f2481d28687b25e63a2be` | SK-002/004/007; OPEN-SKP-01 and OPEN-SKP-03 | Preserve decided geometry separately from open format/color/legacy choices |
| `docs/product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md` | Rev 0.2, Draft; no numeric budgets approved; SHA-256 `20bdeb5883349e00bbe1fb649f3a2477df91e313cddebf8629ab39ad6164e987` | Ledger A vs B; largest block; synchronized peak; budget approval | Point observations and hardware percentiles cannot become peak/performance approval |
| `docs/product/communication-contracts/message-delivery.md` | Rev 0.1, proposal with decisions OPEN; SHA-256 `7ccf959daa5bbf9c711cd71eb2f8e2cf3585a97e9665fa1c022f03060d39edc2` | `COM-OPEN-002` remains open | No delivery or format policy inferred from lab results |

## Findings and fixes

### LAB04-QUAL-CAP-001 — current capture/index status was stale

- **Classification:** confirmed in-scope report defect.
- **Trigger/effect:** the prior report and index said the successful runs were unbound and that current-source hardware evidence was still pending. New manual and scripted runs now have manifests whose source, fixture, sdkconfig, binary, build ID, and build fingerprints all match recomputation from the working source/build trees. Their serial READY marker agrees with each mode/build ID. Leaving the older summary in front would materially misstate the current capture evidence.
- **Fix:** the current report summary and index row now identify the two current-source captures, list current observations, retain incomplete evidence and pending L04-07/DEVICE status, and leave the earlier report as historical context.
- **Verification:** `capture-audit.json`, both current-parser derivatives, source/build hash recomputation, original manifests and serial logs. Original capture files remain unchanged.

### LAB04-QUAL-CAP-002 — no durable raw capture hashes in current summary

- **Classification:** confirmed in-scope audit/evidence defect.
- **Trigger/effect:** a reviewer could not detect alteration or omission of the capture manifest, event journal, original metrics, or serial log from the report. Earlier QUAL review noted hash checking without preserving the hashes. The new user-provided captures require a durable chain of identity for their evidence.
- **Fix:** the report now prints byte length and SHA-256 for all eight raw files (four per run) and links `raw-artifact-hashes.json`, which records paths, bytes and hashes, plus separately hashed derived outputs and capture/build reconciliation.
- **Verification:** hashes were recomputed from source files and compared against the saved audit artifact; the report's values were generated directly from those same file bytes.

## Evidence constraints and non-findings

- Both reparses have 35 trace rows, exact decided inputs `F-malformed`, `F-overflow`, `F-cancel`, PASS experiment verdict, 35/35 raster-counter coverage, and two `resource_sample` events each. The resource samples show the 28,800-byte image and decoded drawing resident at `post_decode_pre_raster`. They remain point observations and do not establish the synchronized lifecycle peak required by L04-07.
- Firmware publication queue `dropped_events=0` is distinct from UART, capture, and parser loss. Firmware trace-loss fields remain UNKNOWN, so the parser correctly reports evidence INCOMPLETE.
- Current-source timing samples supply the requested L04-06 series. `FOUNDATION-LVGL-BUDGET` approves no thresholds; performance stays UNMEASURED and no performance PASS is justified.
- L04-05 reset-gated completion and cleanup are supported by current manifests and serial markers. The serial artifacts alone do not prove visible startup instructions or screenshots; the report keeps such visual assertions unclaimed.
- No format selection, color packing, legacy, checkpoint, seek, cache, or send/delivery decision is closed. `OPEN-SKP-01`, `OPEN-SKP-03`, and `COM-OPEN-002` remain OPEN.
- The change is limited to the LAB-04 report, its evidence-index row, and this new derived-review directory. The earlier QUAL-SUPPLEMENT review and its prior run files are unchanged; their historical raw hashes are newly recorded alongside the current hashes. No additional confirmed in-scope report defect was found on this read-only pass.

No implementation dependency ordering is required for these two report fixes: the capture audit and summary update were done as one coherent qualification refresh. Independent read-only verifier review remains required before accepting the QUAL verdict.
