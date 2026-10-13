# C2 F001 implementation handoff — source frozen

Finding: original [LAB02CAR-F001](../review.md), P1 confirmed prior-message content in an in-range missing slot. Scope is stable card presentation and atomic content clearing only. Governing CAR-GESTURE-1, CAR-BIND-3/4 (with bounded bind guards), CAR-PAGING-3/4 and CAR-CONTRACT; coding standard §§5/7; APP-O-01 exact Loading message… / Message unavailable surfaces. APP-O-02/03 remain untouched. C0 is the prerequisite reproduction; C1a/C1b source is preserved. No dependency ordering required against the unresolved C1b evidence gap.

Source: main HEAD 7ecc594861c1e6fbc9ad647ac591bb44df3acde9 plus baseline WIP, C0, C1a and C1b. `before/`, `before-sha256.json`, `unit.patch`, `after/` and `after-sha256.json` identify exact pre/post unit contents, including untracked tests. `preservation.json` confirms all 13 non-C2 files in the previous unit's provenance are byte-identical. No commits, hardware, private configuration, host/server/Make changes. Source edits stopped before this handoff.

## Causal change

The original null-item path unbound identity and hid the card without clearing labels; the pool then revealed it unconditionally. The pool now consumes a compact session-owned per-slot snapshot (index, revision, session generation, materialized pointer and out-of-range/loading/bound/error state). The view clears sender, preview, duration and unread properties before publishing any new binding, records explicit unbound/loading/bound/error/stale presentation state, hides only out-of-range slots and surfaces every in-range unavailable slot. Loaded neighboring items take precedence over page failure and keep their content. No view, scroll root or scene is recreated.

The production delayed-bind branch stores immutable item plus binding generation/session/revision in existing bounded per-view storage. Each pending bind superseded by reuse receives a `bind_stale reason=superseded` terminal; a mismatched callback receives `reason=snapshot_changed`. Successful completion compares frozen generation/session/revision/ID to view and current owner session/revision before applying labels. Pending content keeps a visible Loading message… surface. `card_state` and successful `bind` traces include session/revision and state.

Existing sender geometry remains; loading/error copy wraps within that label. Bound text uses its existing single-line region. A static 18-byte alpha ellipsis fallback makes U+2026 render in the default font without another widget. Actual PNGs were visually inspected: complete Loading message… / Message unavailable, no tofu or prior fixture text, readable pending-to-bound change. No product geometry or numeric performance budget was introduced.

The accessor borrows the materialized pointer only until the next owner pump. Its compact scalar/pointer structure and 80-byte trace-format buffers replace no ownership rules and introduce no multi-kilobyte UI stack snapshot. Frozen pending item storage already existed in each of three views; extra guard/state fields remain fixed per view. Target stack/memory measurements remain C5/C6 work.

## Self-check evidence

- `final-normal/`: ten actual production C / vendored LVGL scenarios PASS. Original delayed-focus expectation is now an F001 invariant gate; loading clears actual previously unread 0:12 fixture content. New failed-page case proves valid neighbor preservation, visible terminal error and traversal/fetch for new focus. Pool-bounds checks 8,16,64,256 keep exactly three card roots and four children each, unchanged scene and scroll handles.
- `final-slow/`: eleven actual-C/LVGL scenarios PASS with CONFIG_LAB02_SLOW_BIND_MS=100 compiling the production conditional branch. Pending6/superseded7/bound7 frames, generation changes, supersession terminal counts and session-reset stale rejection are asserted. No Python behavior imitation.
- `native-final.log`: all seven native model/session/terminal/provider and architecture gates PASS.
- `firmware-final.log`: actual HTTP scripted ESP32-S3 firmware compile PASS (`make lab.lab02.scripted.build KIT=lynn`). Sandbox attempt failed at ESP-IDF host sysctl access; build-only escalated retry was allowed and passed. No flash. Compiler notices are the existing deprecated LVGL flag APIs, not errors.
- Focused production `git diff --check` PASS. Exact commands, compiler identity, hash-derived cache path, raw stdout/stderr and rendered PPMs retained. Earlier failed test artifacts remain under `/tmp/lab02-c2-fix/` (old assertion and native linkage were corrected before final freeze).

Self-check achieved L0 firmware/host build, L1 native state/guards and L2 actual headless scripted loading/error/delayed-bind rendering. This is **implemented, awaiting fresh independent verification**; no L3, waiver or overall carousel closure. The original fast-boundary blank viewport, reversal/snap supersession and page-during-snap baseline expectations remain and still reproduce their separately assigned findings. Source-hash cache identifies executable inputs for repeatable checks.

## Boundaries / remaining work

F006 retry/control remains separate: no tap control, dead affordance, retry policy or claim of implemented tap-to-retry was added. Existing focus admission can clear a failed page and start another attempt; focused-error test delivers a second terminal to that existing attempt. The misleading header “retrying” was removed in favor of “page unavailable”. Initial Inbox unavailable stage behavior is unchanged pending owner retry decision. F002/F003/F004, mutation semantics and target timing/memory/input evidence are not fixed or closed by this unit.
