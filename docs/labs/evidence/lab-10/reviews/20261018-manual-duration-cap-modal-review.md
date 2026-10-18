# LAB-10 review: manual duration-cap modal

- Finding: `LAB10-F001` (this report)
- Target: `client/labs/lab-10-record-canvas/`
- Baseline: `main`, HEAD `81716c3`; relevant LAB-10 source was clean before this fix.
- Scope: presentation of manual natural duration-cap terminal; no change to the
  capture terminal kind, runner workflow, or experiment verdict.
- User decision: a manual capture that naturally reaches 180 seconds displays
  **Completed**. Early stop stays **Stopped**, cancel stays **Cancelled**, and
  point-cap, queue-overflow, and fault terminals stay **Failed**.
- Unrelated staged, unstaged, and untracked work was preserved.

## Applicable sources

| Source | Authority / requirement | Affected behavior |
|---|---|---|
| `docs/standards/client-application-coding-standards.md` §7 | Normative operational contract: distinguish success, cancellation, timeout, and failure truthfully | Device outcome presentation |
| `docs/product/experiences/sketch-record-and-playback.md` rev 3, SK-008, ACC-SKP-06/07 | Owner-decided manual cap presentation and preservation of scripted qualification | LAB-10 modal |
| `client/labs/lab-10-record-canvas/manifest.lab.json` `manual_terminal_presentation`, `scripted_completion_rule`, `capacity_outcomes` | Active LAB-10 behavior and terminal contract | UI mapping and result classification |
| `client/labs/lab-10-record-canvas/README.md` terminal presentation section | Implementation-facing description and evidence boundary | Implementation and verification |

## Ownership / wait map

```text
capture state machine terminal → LVGL terminal-kind mapping →
screen-state presentation → outcome modal
```

For manual mode, the duration-cap terminal now maps to the completed screen.
Scripted mode continues to wait for scheduler qualification before presenting
completion. Capture terminal counters and host experiment classification are
unchanged.

## Finding LAB10-F001

- Classification / priority: confirmed user-visible defect / P2.
- Trigger: a manual capture naturally reaches the configured 180-second cap.
- Effect: the duration-cap terminal was presented as **Failed**, contrary to the
  owner's decided behavior for normal duration expiry.
- Location: `components/lab10_ui/lab10_ui.c`,
  `lab10_ui_terminal_screen_state()` duration-cap branch.
- Smallest fix: map only non-scripted duration-cap presentation to
  `LAB10_SCREEN_COMPLETED`; retain scripted qualification and every other
  terminal mapping.
- Verification boundaries: manual duration-cap; early stop; cancel; point cap;
  queue overflow; fault; scripted accepted, rejected, and unresolved
  qualification.
- Dependencies: none. No dependency ordering required; one UI mapping fix was
  followed by focused boundary tests, independent source review, manual build,
  and target run.

## Outcomes and evidence

- Implementation: completed in `components/lab10_ui/lab10_ui.c`; test and
  contract updates in `tests/test_lab10_scripted.c`, `manifest.lab.json`,
  `README.md`, and the experience specification.
- L1: `python3 -m unittest tests/test_lab10_scripted.py` — 6 tests passed;
  `python3 -m json.tool manifest.lab.json` — passed; scoped `git diff --check` —
  passed. A fresh read-only verifier independently checked the mapping, callers,
  boundary cases, and tests.
- L0: `make lab.lab10.build KIT=lynn-kit` — passed. Initial sandboxed attempt
  hit ESP-IDF/psutil process-enumeration restrictions; retry with process-table
  access passed.
- L3: after flashing and running `20261018-manual-run-0e0584ba` for the natural
  180-second interval, the user confirmed the device displayed **Completed**.
  No screenshot was attached.
- Run artifacts: `docs/labs/evidence/lab-10/runs/20261018-manual-run-0e0584ba/`;
  raw serial: `logs/device/labs/lab-10/20261018-manual-run-0e0584ba/serial.txt`.
  The host run is `workflow_status=PASS`, `experiment_verdict=UNMEASURED`,
  scenario `not_configured`, and `trace_present=false`; serial has no device
  capture-terminal or `LAB10_EVIDENCE` marker. Thus the visual modal behavior is
  user-confirmed, but this run did not produce drawing or quantitative
  experiment measurements.
- Disposition: **verified fixed for the device-visible modal** (L3). Scripted
  qualification and early-stop/cancel/failure behavior remain covered by L1
  tests. Experiment measurement remains unverified/unmeasured and is not
  implied by this fix.

## Measurement boundary

`workflow_status=PASS` describes the host runner/build/parser/cleanup lifecycle;
it is not a measured experiment pass. For run `20261018-manual-run-0e0584ba`,
the parser reports no trace and zero samples for device-derived metrics. The
manual workload name selects an operator case but does not generate a device
trace. This run did not measure drawing latency, touch points, rendering, or
audio. Measurement requires the corresponding device trace/instrumentation and,
for visual/scanout/audio claims, synchronized external screen/audio evidence.
No measurement-instrumentation change was included in this finding.
