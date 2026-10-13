# C3a F004 fixer handoff

Source is frozen for fresh independent read-only verification. Original complete
finding and owner authority are in before/docs/labs/evidence/lab-02/reviews/carousel-remediation/{review.md,execution.md}.
HEAD: 7ecc594861c1e6fbc9ad647ac591bb44df3acde9 plus preserved WIP/C0/C1a/C1b/C2.
`before/` and `after/` retain this unit's source snapshots; source-sha256.json
records exact bytes. unit.diff is the unit-only logical diff. README's original
mixed line endings are preserved. preexisting.diff and preservation.json prove
unrelated tracked changes stayed identical, including Make/host/harness/Wi-Fi.

## Change and bounds

One owner-held static immutable typed page event retains only a newer page-zero
mutation during motion. Ordinary same-revision pages apply/terminalize during
motion. One pump accepts at most one completion OR applies one pending mutation;
pending application takes precedence once unlocked. Stages/transport retain their
independent bounded mailboxes. All fetches retain C1a exactly-once reservation and
terminal emission. The pending snapshot never points into provider, mailbox or
view state; item snapshots remain valid only before next pump. No blocking API,
large automatic buffer, scene rebuild, node-count or geometry change is added.

Accepted mutation fetch success, pending revision, applied revision/page_apply,
and actual card bind publication are distinguishable. Superseding a pending
mutation emits model_pending_stale; resetting the session emits session_superseded.
Latest owner focus is used at reconciliation instead of the arrival-time index.
Existing viewed ID and removal tie/previous-if-last behavior are preserved.

## Self-check evidence (not independent verification)

- baseline-native.log: four real native C gates PASS with old assertions.
- baseline-lvgl/: actual renderer baseline reproduces F004 page-during-snap.
- native.log: four native gates PASS; terminal-final.log reruns the changed latest
  focus intent test. terminal/: six original terminal scenarios plus pending
  mutation/supersession and session-reset cases, unique terminal identities.
- startup/: actual production HTTP provider boot/background allocation failure and
  success remain PASS; existing stage/admission/queue-pressure contracts preserved.
- lvgl/ and lvgl-slow/: actual LVGL normal and 120ms slow-bind scripted gates PASS.
  held-page loading/bound PPM+PNG proves in-place loading conversion with viewport,
  root identity and lock retained for >1 second. Long animated page-during-snap
  accepts ordinary page while keeping viewport/root identity. That scenario does
  not imply viewport coverage: fast traversal blank F002 remains characterized.
  Push/remove preserve revision1 until terminal, then coherent revision2 and m8/m9.
- firmware-final.log: actual HTTP scripted ESP32-S3 build PASS, no flash.
  Initial sandbox build failed on host sysctl. First escalated call was rejected
  by approval service usage limit; resumed escalated build succeeded.

Evidence reaches self-check L0/L1/L2. F004 is implemented awaiting independent
verification and remains overall unverified without L3/waiver. No target access,
resource high-water/timing claim or final carousel closure. F002/F003/F006 and
APP-O-05 four-view geometry/APP-O-04 initial retry belong to later units.
