# Independent C0 verification

Verdict: C0 accepted as baseline characterization. No finding is fixed. C1a prerequisite (actual-path ownership/queue characterization) is satisfied at host L1/L2 only.

## Source provenance and authority

Fresh read-only verifier distinct from implementer; no inherited fixer conversation. Read full client-code-review skill, canonical workflow, coding standard, docs/AGENTS, applicable carousel plan, execution owner decisions, original complete review, actual C0 diff/support files and production neighbors. Main HEAD 7ecc594861c1e6fbc9ad647ac591bb44df3acde9 plus preserved WIP. source-check.json confirms unchanged maintained source/docs during verification, byte-identical tracked diff to pre-existing-wip.patch, identical handoff native-input manifest, and matching hashes for all five C0 files. before/after-sha256.json identify reviewed source. No hardware, network service, private config, source or maintained-document edits.

## Commands and results

- python3 scripts/tests/test_lab02_carousel_native.py --artifact-dir /tmp/lab02-c0-verify: PASS, eight actual-path scenarios; commands.json, configure-final.log, build-final.log, scenario logs and framebuffers preserved.
- python3 -m unittest scripts.tests.test_lab02_model_native -v: PASS, two existing actual-C checks, no skips; model-checks.log.
- Scratch no-motion source compiled using the original CMake flags and linked to the independently identified production objects/LVGL library: PASS as characterization; exact commands in boundary-commands.json, source scratch-no-motion.c, logs boundary-compile.log, boundary-link.log, no-motion.log. Maintained files unchanged.
- PPM frames converted to PNG using bundled runtime Pillow; screen-correlation.png visually inspected. System Python lacks Pillow; initial conversion attempt failed, bundled-runtime conversion passed. This did not affect native checks.

## Scenario observations / levels

- Fast boundary: focus remains4 while scroll2798 exposes viewport20; all three roots have negative offscreen coordinates; framebuffer has zero card background pixels. At release focus20 binds missing identities while retaining old labels. L2 headless controller/card/LVGL reproduction of F002 and F001.
- Delayed focus: in-range center surface visible with id cleared and Sender4/Fixture4 despite focus20; actual page1 arrival binds m20. Trace and rendered frames agree. L2 for F001.
- Reversal: reverse displacement to2 while snap8 active is ignored; eventual focus8; programmatic replacements leave3 starts and2 ends. L2 for F003.
- Page during snap: real session event is deferred, item20 absent while locked; after terminal it applies and renders m20. L1 injected session plus L2 actual animation/rendering for F004.
- Push during snap: pre-submitted append poll response revision2 deferred; terminal count65 preserves m8. L2 host identity continuity, no managed-server claim.
- Removal during snap: pre-submitted removal revision2 deferred; terminal count63 selects m9 at index8. L2 host identity continuity for non-last deletion only. Synthetic fixture rewrites sender/ID but retains original preview; rendered Sender9/Fixture8 is supplied input, not evidence of a production stale-preview defect.
- Stale after revision: revision2/focus8 persists; old request rejected op_mismatch, apply_stale1, item20 absent. L1 injected reducer plus L2 renderer checkpoints.
- Queue full: actual eight-entry queue saturates; accepted op1 terminal drops, drop counter1, page1 absent, no matching apply terminal. L1 production session fault injection; L2 software-render checkpoint, not actual FreeRTOS/worker concurrency confirmation of F005.

Extra boundary: same-target animated set_focus4 emits snap_end id0 without snap_start or new ID (starts delta0, ends delta1). Both locks clear and identity remains m4. This reproduces an already documented F003 boundary; no new regression was introduced by C0.

## Limits and disposition

CMake compiles actual vendored LVGL plus production session/controller/pool/card/snap/instrumentation/model/metadata. Existing pthread RTOS stubs, direct response producer delivery, provider initialization/item-lookup adapters and synthetic pages replace board/input/network scheduling. Immediate binds configured; no slow-bind/hardware velocity simulation. Source-hash/compiler digest selects cache 99729362fdd7c5f9c687; configure/build executed and saved against that cache. Current exact source provenance is adequate; no portable guarantee across differing compiler environment flags is implied.

No production/WIP behavior edits or regressions detected. F001-F005 remain not fixed and runtime-characterized within the stated host limits. F006 retry and F007 allocation remain not fixed and not newly runtime-verified by this C0 harness. C0 does not close target input/timing/memory/lifecycle/visual or actual network-worker behavior; no L3 and no waiver. Last-item removal, stale snap callback injection, initial terminal pressure, real worker allocation faults, extended inbox sizes and numeric budgets remain later-unit evidence. Characterization assertions deliberately expect current defects and must become acceptance expectations in relevant later fixes, while these baseline artifacts remain retained.
