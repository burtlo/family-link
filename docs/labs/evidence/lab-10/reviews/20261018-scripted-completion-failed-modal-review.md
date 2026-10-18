# LAB-10 review: qualifying scripted completion shown as failed

- Review run: `20261018-L10-TERMINAL-001`
- Target: `/Users/lynnfrank/src/family-link/client/labs/lab-10-record-canvas/`
- Scope: terminal result presentation for the scripted 180-second capture
- Branch / HEAD: `main` / `eb7387a173f7c1968c9bc0cb6158c78ab0613a6b`
- Mode: review, implement, independently verify; implementation was authorized
  earlier in this task
- WIP: target LAB-10 source was clean at HEAD; unrelated LAB-02/LAB-03/LAB-05
  changes and untracked data were preserved. Reviewer's tracked working diff
  hash: `a2e0f03bf31cf154ceba87bf0fd3b9ab6268253505b8a8dcfa729db0ac4a9e4b`.
- Review agent: `01a15101-a5b0-7363-9604-dd1569ae7979` (read-only)
- Source provenance: `main/lab10_ui_lvgl.c`
  `6fd1413d0c14c44553b7f01bd6a16c19df7ae6ad6917f68ab459b786e3158cff`;
  `components/lab10_ui/lab10_ui.c`
  `379de7c2a2acabb70f32bd994547a24881842778be0baae896fb028f011b1004`;
  `components/lab10_ui/include/lab10_ui.h`
  `e2f7fc149d636afd782d5efc3b3d802e970747d669b549822b454a461690451e`;
  `manifest.lab.json`
  `f9781cbaf7477898d37a2ef0bc145e1febb2407bf5029e66cfdca6e5329f025c`;
  `docs/product/experiences/sketch-record-and-playback.md`
  `13cc4b8184b2c0d4dfc54baed2d00e7bf0092a7603838005a4876e35c39bb36e`.

## Applicable sources

| Source | Authority / requirement | Affected path |
|---|---|---|
| `docs/standards/client-application-coding-standards.md` §§3, 5, 7, 9, 11, 12 | Normative: one session transition owner, truthful terminal outcome and failure UX, and verification-level claims | Capture terminal → screen state → outcome panel |
| `client/labs/lab-10-record-canvas/manifest.lab.json` `scripted_completion_rule`, `scripted_device_workload.completion_requires`, `capacity_outcomes` | Active LAB-10 contract: only a qualifying duration-cap run emits `status=complete`; premature termination, point cap, queue overflow, and failure cannot pass | Scripted terminal UI |
| `client/labs/lab-10-record-canvas/README.md` L10-06/L10-08 scripted protocol | Script continues through 180-second duration cap; qualified completion requires bounded counts, colors, and clean trace | Scripted runtime and evidence |
| `docs/product/experiences/sketch-record-and-playback.md` rev 2, SK-005/SK-006 | Owner-decided canvas/multicolor intent; exception/failure presentation remains Partial/Open | Canvas outcome semantics; no new non-scripted UX policy inferred |
| `docs/v1-assessments/v2-implementation-practices.md`, “Practices for each v2 feature slice” | Engineering implementation guidance, not product authority | Review and verification gates |

The specified scripted path unambiguously treats a qualifying duration-cap as
completion. The experience does not settle non-scripted duration-cap copy; that
remains OPEN and outside this fix.

## Ownership / wait map

```text
scripted scheduler → touch routing → capture FIFO/state machine → terminal stats
        → scheduler qualification + terminal UI mapping → LVGL outcome panel
        → atomic marker mailbox → app_main serial markers / host parser
```

The UI task owns capture polling, scripted qualification, and widget updates.
`app_main` drains the atomic terminal marker handoff and does not mutate LVGL.
The device capture and host workflow can therefore correctly report completion
while the UI independently maps the same terminal outcome to the wrong label.

## Finding LAB10-F001

- Classification / priority: confirmed user-observed runtime defect, P1
- Trigger: run scripted drawing through the qualifying 180-second duration cap.
- Effect: the capture reaches `duration_cap`; the scripted scheduler accepts
  completion; the UI maps the terminal to `LAB10_SCREEN_FAILED` and displays
  “failed.” This contradicts the device and host completion evidence.
- Requirement: manifest `scripted_completion_rule` requires a duration-cap
  terminal and successful qualification before `status=complete`; coding
  standard §§3, 7, and 9 require truthful terminal presentation.
- Location: `client/labs/lab-10-record-canvas/main/lab10_ui_lvgl.c:1337-1353`
  (mapping); `:1038-1048` (visible outcome text).
- Smallest fix: for the scripted path only, map duration-cap to the successful
  screen state only after the scripted scheduler accepts the completion
  predicate. Keep an unqualified duration-cap and all other failure terminals
  on the failure path. Do not change the OPEN non-scripted duration-cap policy.
- Verification: L1 tests for qualifying scripted duration cap, unqualified
  duration cap, point cap, overflow, and ordinary failure mapping; L0 manual
  and scripted builds; L2 scripted target capture; L3 human observation of the
  resulting modal.
- Dependencies: none. No dependency ordering required.

## Evidence

Run `20261018-scripted-run-af4ca88f` artifacts:

- Raw serial:
  `logs/device/labs/lab-10/20261018-scripted-run-af4ca88f/serial.txt`
- Manifest, metrics, and event journal:
  `docs/labs/evidence/lab-10/runs/20261018-scripted-run-af4ca88f/`

Serial reports one `LAB10_CAPTURE_TERMINAL outcome=duration_cap`, followed by
`LAB10_SCRIPTED_DRAWING status=complete` with 3,416 accepted points, 18 strokes,
17 palette changes, color mask `0x3e`; its diagnostic reports zero queue
overflow and trace loss, one terminal event, scheduler acceptance, renderer
drain completion, and all color masks `0x3e`. The scenario is `passed` and host
workflow is `PASS`; experiment verdict is `UNMEASURED`. The user initially
reported the device modal said “failed”; the later remediation and user-visible
confirmation are recorded under Outcomes below. No screenshot/video is attached.

## Commands and results

- Read `docs/AGENTS.md`, the full client coding standard, canonical review
  workflow, LAB-10 README/manifest, experience source, implementation practices,
  source, and run artifacts.
- Fresh read-only reviewer `01a15101-a5b0-7363-9604-dd1569ae7979` confirmed the
  mapping defect and reviewed state/hash provenance. It ran no tests, builds,
  or device actions, as required by its assignment.
- This section records the initial read-only review; remediation, verification,
  and target runs are recorded under Outcomes below.

## Evidence levels

- L2 device run artifact: scripted duration-cap, scheduler completion, renderer
  drain, and scenario pass are captured in raw serial and run metadata.
- Initial L3 user observation: the visible modal said “failed”; no visual
  artifact independently attached. The post-fix L3 observation is recorded below.
- Host workflow: `PASS`; experiment verdict: `UNMEASURED`. These claims remain
  distinct from the visual defect.

## Execution plan

No dependency ordering required. The initial plan was completed: the defect was
implemented without changing non-scripted duration-cap policy, independently
verified, and exercised in repeated scripted target runs. Run evidence and the
user's resulting L3 modal observation are recorded below. Unrelated WIP was
preserved.

## Outcomes

- LAB10-F001: confirmed; scripted UI qualification guard implemented and
  independently verified at L1. Three consecutive profile-identified device
  runs (`20261018-scripted-run-c4d0e447`, `20261018-scripted-run-a50c8b94`,
  and `20261018-scripted-run-f6022a36`) passed duration-cap completion with
  respectively 3,518, 3,514, and 3,514 accepted points; all had 18 strokes,
  17 palette changes, mask `0x3e`, no queue/trace loss, renderer drain complete,
  and scheduler acceptance. For run `20261018-scripted-run-c4d0e447`, the user
  confirmed the final device modal displayed “Completed” (L3; reported in this
  conversation on 2026-10-18).
- Non-scripted duration-cap presentation: OPEN; not changed by this finding.
- Independent verifier: `01a1510a-2a35-7c30-86bb-a38080f1a949` reviewed the
  guard and reported source/host checks passing; it did not perform target
  actions. The target run above is subsequent L2 evidence.
- Final modal text: user-confirmed “Completed” for
  `20261018-scripted-run-c4d0e447`; no screenshot/video was attached.
