# LAB02CAR-F010 implementation handoff

Baseline HEAD: 21a37df4b220eabcf6b3717601753cc8cf804360. Root review/execution documents and c4-proof artifacts are owned by root and untouched. Source is frozen for independent verification; no commit made.

## Change and causal trace

Only production lab_main.c and carousel_controller.c changed. Exact unit.patch removes the successful HTTP startup branch that calls lab02_board_status_set("loading inbox…") and changes only the loading-shell header string to "LAB-02". board.c creates the separate centered screen child lazily; deleting its successful-startup call prevents that label from being created for loading. True init/task-start failure calls remain unchanged. Header still returns from the shell branch, so shell is not falsely labeled empty; subsequent READY and ERROR branches continue to render domain status. APP-O-01 loading/error cards, their ellipsis fallback, APP-O-04 initial error click/retry handler, geometry, draw buffer50 and F009 identity paths are untouched. Exact byte substitutions and original mixed line endings are confirmed in preservation-and-source-proof.json.

## Self-check results

- git diff --check: PASS (diff-check.txt).
- Actual LVGL, production controller/session/provider with explicit RTOS/HTTP task adapters: temporary runtime-source test instrumentation prints and asserts startup shell LAB-02, retry shell LAB-02, terminal success item header and terminal failure unavailable header. retry-success and retry-boot-fail PASS; actual click enters new retry attempt and reaches READY/ERROR. Logs and terminal PPM frames are saved.
- One existing normal delayed-focus runtime gate: PASS; in-range card surfaces and loading content clearing remain correct.
- Production HTTP manual non-eager ESP-IDF firmware build-only: PASS (firmware-build.log). Existing config has nonempty HTTP URL, draw buffer height50. Secret values are not copied into evidence. Firmware bin is0x153fa0 bytes with0x23060 free in app partition; SDK configuration and ELF/bin hashes saved for provenance.

Temporary CMake harness copies the existing initial-retry test under /tmp and adds header state assertions/printing. It compiles the actual production code against vendored LVGL; no maintained implementation test edits. runtime-source-sha256.json identifies production/test inputs; runtime-commands.json records executions.

## Evidence and limits

L0: production firmware compile and exact source/call-path proof. L1/L2: actual headless LVGL header shell/retry/ready/error transitions and existing normal loading-card rendering plus F008 regression gates. The runtime harness does not execute lab_main.c or hardware board.c, so centered-board-label absence is a source proof only. Owner observed original physical overlay; no new flash, network request, hardware interaction, physical before/after screenshot or L3 claim. Independent verification by a separate fresh read-only agent remains required. Target confirmation of the removed physical overlay remains open.

Changed source after hashes are in after-sha256.json. Original source copies, before hashes, unit.patch and unchanged-root-report hashes are retained here.
