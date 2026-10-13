# C3b / LAB02CAR-F002 implementation handoff

Source frozen 2026-10-13. Parent supplies original full finding and requirements in
`docs/labs/evidence/lab-02/reviews/carousel-remediation/review.md` and `execution.md`.
APP-O-05 approves existing 320/132/12 geometry with four roots. C1a/C2/C3a L1/L2
mechanism prerequisites are accepted; their target gaps remain distinct.

## Causal change

Old BEGIN/END-only controller leaves the old three-root window behind on fast
traversal. Before actual rendered drag20 contains zero card-background pixels.
SCROLL now presents the actual first/last half-open card intersections on every
displacement, including custom snap animation; session target focus remains intact
while snap owns it. Four roots / 20 card nodes cover the x566 four-partial case.
No scene or scroll tree is recreated and card count is independent of N.
Surviving index/session/revision/ID/state bindings stay in their existing root,
including pending delayed timers; only vacated roots recycle. Focus styling is
Level 0, bounded recycled content is Level 1, and no new UI-stack item copy exists.

The presentation holds one coalesced latest focus intent, submitted at most once
per 20ms plus immediate gesture-end flush. Domain focus is applied only by the
session; controller optimistic assignments were removed. Focus page admission
precedes speculative neighbors. Full/busy admission retains one latest unadmitted
page, retried nonblocking at most once per 20ms during motion. Admission,
materialization, failed page or superseded focus stops this path. Existing failure
policy is preserved; no F006 failed-page control or F003 supersession is claimed.
The interval is an engineering rate/storage bound, not an approved product gate.

## Evidence

- Baseline actual LVGL PASS characterization: original drag20 blank, zero pixels.
- Final normal actual LVGL: all 11 scenarios PASS; final slow (120ms): 12 PASS.
  Every advance tick asserts actual visible interval coverage; 8/16/64/256 sweep
  covers every index at zero/quarter/half/stride-minus-one offsets and edges.
  Selected x566 frame shows indices 3/4/5/6, with partial outer cards.
  Fast path covers materialized local content, multi-page missing jump, reversal
  and immediate end-flush. Delayed loading content retains truthful surfaces.
  Root/node counts remain 4/20, screen/scroll handles remain unchanged.
  Normal drag20 now contains 37,861 card-background pixels.
- Seven native/model/architecture unittest gates PASS (native-results.log).
  Full and busy admission probes preserve final owner focus 176 and admit latest
  page 11 after capacity returns while scroll remains locked; no duplicate enqueue.
  Existing terminal delivery, startup allocation and owner deferral regressions pass.
- Final HTTP scripted firmware compile PASS (firmware-build-final.log). Sandbox
  initial failure was macOS sysctl resource query; authorized build-only escalation
  succeeded. No device was flashed. Final production first/last geometry refinement
  is included in the final incremental compile.
- Inspected selected renderer PNGs: loading surfaces during drag and four partial
  intersections. Transport and RTOS are adapters; LVGL rendering and production
  controller/pool/session/snap/model are actual code.

## Provenance and limits

`before-working-tree.diff`, `before-status.txt`, `before/`, and `before-sha256.json`
identify original HEAD plus WIP. Baseline runtime source manifest independently
matches every runtime-owned before file, including initially reconstructed native
wrapper snapshots (`before-provenance-check.json`); the model wrapper snapshot also
matches C3a frozen after source. `unit-byte.diff` is exact byte diff and `unit.patch`
is normalized readable diff. `after-sha256.json`, `changed-files.json`, final
normal/slow runtime manifests and frozen-source checks identify handoff state.
`preservation.json` proves non-unit tracked WIP diff segments remain identical,
including Make, host, Wi-Fi, harness, provider, prior card-view and prior session
contracts. Existing eager implementation is unchanged; shared geometry is retained.
README governing relative links repaired and four-view authority/resource limits
linked; architecture describes engineering bounds and preserved identities.

These are fixer self-checks at L1/L2 and L0 build, pending a fresh read-only verifier.
No L3, waiver, target input timing, stack, fragmentation or physical visual claim.
F003 reversal still intentionally reproduces old target; F006 remains separate.
Numeric product gates and target confirmation remain OPEN for C5/C6.
