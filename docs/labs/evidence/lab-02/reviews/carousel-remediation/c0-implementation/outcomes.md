# C0 baseline handoff

Source baseline main HEAD `7ecc594861c1e6fbc9ad647ac591bb44df3acde9` plus pre-existing WIP in `pre-existing-wip.patch`. Only five new support files changed; exact patch `c0-only.patch`, hashes `changed-files-sha256.json`. `tracked-wip-unchanged.txt` records byte-identical tracked diff before/after. Production sources, Makefile, host orchestration, private configuration and server files were not edited.

Final commands:

```
python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c0-baseline
LAB02_C0_ARTIFACT_DIR=/tmp/lab02-c0-baseline python3 -m unittest scripts.tests.test_lab02_carousel_native scripts.tests.test_lab02_model_native -v
```

Both passed. The unittest pass contains one native carousel gate (eight scenarios) and two existing actual-C model/session gates; no skips. CMake native compilation succeeds with warnings from vendored API deprecations and existing production code, not a firmware build. First scaffold compile failed due missing stdio include; corrected; original raw build.log retained. First fast-drag trial used scroll_to_x which synthesizes complete events; corrected to LVGL input raw displacement. First mutation trial attempted refresh under lock, and correctly found submission rejection; corrected to pre-submit poll and deliver response during snap. These development outputs remain separate from final passing scenario logs.

| Scenario | Observed baseline | Evidence |
|---|---|---|
| fast-boundary | Drag from focus4 to viewport20 leaves focus4, three roots offscreen; no card background pixels rendered. End then rebinds to unavailable20 with stale labels. | fast-boundary.log; fast-boundary-drag20.png; pixel-observations.json |
| delayed-focus | Unmaterialized focus20 has identity-cleared visible center root retaining Sender4/Fixture4. Page arrival finally displays m20. | delayed-focus.log; delayed-focus-missing.png; delayed-focus-terminal.png |
| reversal | Reverse to viewport2 during snap8 is discarded; terminal focuses8. Programmatic replacement sequence produces 3 starts and 2 ends, no stale terminal for deleted snap. | reversal.log; reverse2/old-target/replacement frames |
| page-during-snap | Page1 response is deferred; item20 remains absent during animation; materialized and bound only at terminal. | page-during-snap.log and arrival/terminal frames |
| push-during-snap | Pre-submitted poll with append revision2 arrives while snap8 runs; revision remains1 until terminal; revision2/count65 then preserves m8. No defect inferred from this passing continuity boundary. | push-during-snap.log and frames |
| remove-during-snap | Pre-submitted poll deletes focused m8 during snap; visual revision1 persists until terminal; revision2/count63 selects m9 at same index8. Matches owner next/previous-last for this non-last boundary. | remove-during-snap.log and frames |
| stale-after-revision | Focus moves4→8, revision2 applies; old page1 revision1/op response is rejected as op_mismatch, apply_stale=1, item20 remains absent. | stale-after-revision.log and frames |
| queue-full | Eight actual queued transport events saturate completion queue; accepted page1 terminal is dropped, fetch_event_dropped=1, page1 remains absent and no page terminal trace applies. | queue-full.log; terminal-lost frame |

`characterization=PASS` deliberately means the baseline observation, including defects, reproduced. No finding is fixed. C0 gives L2 headless software rendering for controller/card/snap paths and L1 injected session/queue checks. Actual C session, model, controller, pool, card, snap, instrumentation and LVGL are compiled. RTOS primitives are existing pthread stubs; HTTP initialization/item lookup and response delivery are host adapters. Synthetic C pages match lab server append/removal shape, but no server, physical pointer driver, network worker, board, target timing/memory/resource evidence, or hardware visual continuity is claimed. No verifier waiver supplied. Numeric budgets and three-view geometry remain OPEN.

Full inputs and exact native commands are saved in source-sha256.json, runtime.json and commands.json. Every checkpoint has a PPM framebuffer and PNG conversion, with C0_STATE listing session/revision/focus IDs, snap IDs/locks, slot binding IDs/generations/hidden flags/coordinates and fixture labels. C0_REQUEST/C0_DELIVERY plus production page_apply/apply_stale/deferred and snap_start/end traces correlate operations; missing terminal outcomes remain visible as baseline defects rather than synthesized successes.

Repository editing stopped at this handoff. Independent fresh verification remains root's next action.
