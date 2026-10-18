# LAB-10 scripted workload timing review

## Finding LAB10-F002

**Priority:** P1 · **Status:** Confirmed; remediation pending

The scripted 180-second workload can reach its duration cap before meeting its
minimum accepted-point requirement. In run `20261018-scripted-run-ffc880a2`,
the device captured 3,201 points, 17 strokes, and 16 palette changes. The trace
had all required colors (`0x3e`), no queue overflow or trace loss, exactly one
duration-cap terminal, and a complete renderer drain, but the scheduler did not
accept completion because the point count was 199 below the 3,400 minimum. The
host therefore reported `INCOMPLETE` because the scripted completion marker
was missing.

The deterministic cadence averages 44 ms, and the planned 18 strokes add
400 ms pen-up gaps and 780 ms inter-stroke pauses. The nominal plan consumes
nearly the full 180-second capture window before 20 ms LVGL polling quantization
and UI-task work, leaving insufficient timing margin. A prior run
(`20261018-scripted-run-af4ca88f`) did qualify with 3,416 points, 18 strokes,
and 17 palette changes, confirming the current schedule is timing-sensitive.

The smallest contract-preserving correction is to bias the deterministic move
cadence toward the faster end of the already declared 36–52 ms range, keeping
the declared gaps, pauses, 180-second capture, and completion requirements
unchanged. The native scheduler test should exercise the actual 20 ms poll
cadence and verify that all planned strokes complete with margin, while
retaining rejection cases for non-duration terminals and insufficient points.

## Evidence

- Device serial: `logs/device/labs/lab-10/20261018-scripted-run-ffc880a2/serial.txt`
- Host metrics: `docs/labs/evidence/lab-10/runs/20261018-scripted-run-ffc880a2/metrics.json`
- Workload contract: `client/labs/lab-10-record-canvas/manifest.lab.json`
- Scheduler and native tests: `client/labs/lab-10-record-canvas/components/lab10_ui/lab10_ui.c`, `client/labs/lab-10-record-canvas/tests/test_lab10_scripted.c`

## Remediation and independent verification

The deterministic profile was tuned to 36/36/36/40/40 ms within the declared
36–52 ms bounds. A deterministic 20 ms-poll native test now requires all 18
strokes and 17 palette changes, at least 3,400 accepted points, and the final
event by 170 seconds. The first target retest still ended incomplete, producing
3,205 points; this showed the fixed-step native model did not capture actual
device poll lateness. Terminal-only scheduler telemetry and a required
cadence-profile marker were then added without per-point logging or catch-up
behavior.

Fresh read-only verifier `01a1511d-d060-7573-96c4-b490bad17527` independently
passed the profile/adapter checks, parser/schema compatibility, and both host
suites (71 LAB-10 package tests and 38 adapter tests). It verified the
cadence-profile requirement and accurate operator banner. Three consecutive
target runs (`20261018-scripted-run-c4d0e447`,
`20261018-scripted-run-a50c8b94`, and `20261018-scripted-run-f6022a36`) report
`workflow_status=PASS` and `experiment_verdict=UNMEASURED`. Each reports:

- `workflow_status=PASS`, `experiment_verdict=UNMEASURED`
- profile `scripted-cadence-36-36-36-40-40-v2`
- 3,518 / 3,514 / 3,514 accepted points; all 18 strokes, all 17 palette
  changes, mask `0x3e`
- zero queue overflow and trace loss; one duration-cap terminal; renderer drain
  complete; scheduler accepted completion
- About 4,240 UI polls, maximum poll gap about 70 ms, roughly 3,500 late
  events, and maximum event deadline lateness 48,000 µs

The three target runs provide repeat L2 workflow/runtime evidence that the
scripted run qualifies with the profile and telemetry. For
`20261018-scripted-run-c4d0e447`, the user confirmed the visible device modal
displayed “Completed” (L3; reported in this conversation on 2026-10-18). No
performance or experiment verdict is claimed; the experiment verdict remains
`UNMEASURED`.
