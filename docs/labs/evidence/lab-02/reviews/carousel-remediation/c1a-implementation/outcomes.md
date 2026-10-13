# C1a implementation handoff — stop editing; independent verification pending

Finding: carousel-remediation/review.md / LAB02CAR-F005, confirmed P1.
Authority: CAR-ARCH-2/3, CAR-PAGING-1 and client standards §§2–4. Main HEAD
7ecc594861c1e6fbc9ad647ac591bb44df3acde9 plus recorded baseline WIP and accepted C0.
C0 queue-full.log independently reproduced accepted terminal loss before this fix.

## Change and causal result

Eight normal request admissions reserve eight terminal mailboxes; initial admission
has a dedicated ninth mailbox. Reservation precedes queue submission and survives
producer completion and motion deferral. Owner reconciliation emits exactly one
fetch_terminal before admission can be reused. Transport freshness is one coalesced
mailbox; stage reports occupy an ordered bounded bitset, preserving CONNECTING then
FETCHING even when both arrive before the pump. Neither can displace a terminal.
Workers only write delivery records protected by the producer mutex. They never
clear inflight/domain state. Duplicate/unadmitted results are rejected observably,
without replacing accepted terminals. The session owner terminalizes superseded
session/revision/operation reservations as stale, including before a producer
returns, so late results cannot clear newer operations.

The immutable request freezes session/op, revision, page index/offset/size, attempt,
submission timestamp and response-read timeout budget. HTTP consumes frozen size
and offset. Each accepted retry has a new op identity and attempt=1. Existing HTTP
3-second call timeout and 12-second response-read phase remain unchanged. A
whole-attempt deadline covering queue wait and all HTTP phases remains an explicit
C1/C5 gap, rather than claiming the read deadline is that guarantee.

The owner uses zero-timeout mutex/queue operations; a busy producer leaves bounded
work pending. There are at most nine full-page delivery records plus one pump copy,
eight request queue entries, one freshness mailbox and one stage bitset. Worker
producer copies may wait for the bounded producer mutex; presentation never does.
Motion success deferral, model/render/snap behavior and existing retry UX remain
unchanged. Transport freshness can now be applied even while a page is deferred;
this is necessary to remove shared terminal-capacity pressure. Wi-Fi join failure
now reports a boot fetch failure, reaching the same owner ERROR stage while also
terminalizing its accepted initial reservation.

## Evidence and self-checks

- focused-final.log: six unittest gates PASS, no skips. Actual C parser/session and
  native pthread adapters; test_terminal_native.c runs six scenarios, with 29 total
  uniquely identified owner fetch terminals. Raw term traces in terminals/*.log.
- initial-saturation: all eight normal reservations plus boot terminal stored;
  transport burst and duplicate boot result cannot prevent READY. Revision-0 normal
  operations each finish stale after boot revision1; no accepted terminal lost.
- initial-timeout: duplicate differing completion does not replace the first timeout;
  owner ERROR, one timeout terminal. Invalid subsequent READY stage is rejected.
- normal-saturation: eight terminals retained during real owner motion deferral;
  concurrent 10,000 transport updates do not free capacity or overwrite results;
  unlock applies success and seven timeouts exactly once, then admits new work.
- newer-operation: page0 revision2 supersedes old page1; owner stale terminal emitted
  before late old producer returns. Concurrent late failure/transport and current
  duplicate result preserve revision2/current page1 success and zero fetch failures.
- response-old-revision: old response revision rejected, new op succeeds; duplicate
  after consumption produces no second terminal.
- stale-session: reset/new boot preserves old owner stale terminal; late old page
  cannot populate new session content.
- headless-final/: all eight existing actual production session/controller/card/pool/
  snap + vendored LVGL scenarios PASS. queue-full acceptance now requires retained
  page and zero dropped terminals. Other baseline defect expectations stay intact.
- http-build-final.log: authorized make lab.lab02.scripted.build KIT=lynn PASS,
  compiling actual ESP-IDF production HTTP consumer and firmware. Original sandbox
  attempt failed at toolchain sysctl; one allowed rerun plus final incremental build
  passed. No flash/capture/server orchestration. Compiler commands and output image
  hashes retained. Build logs contain no SSID/PASSWORD/SERVER_URL or private URL values.
- Changed tracked C1a source/docs whitespace check passed. Whole-tree diff --check
  detects preserved pre-existing CRLF WIP warnings in host scripts; no edits there.

## Provenance and boundary

changed-files.json lists all 13 changed files. c1a-only.patch is relative to exact
pre-C1a contents, including previously untracked C0 files. before-source/ stores
those bytes; before-sha256.json and after-sha256.json identify the exact state.
Reconstructed C0 bytes match accepted C0 hashes. preservation.json confirms all
pre-existing tracked WIP diffs byte-identical. before-working-tree.diff retains
that WIP. commands.json and headless-final/commands.json record raw invocations.
Source edits stopped; a fresh independent verifier must decide the finding status.

## Limits / remaining scope

Achieved implementation self-checks: L0 firmware build; L1 actual C injected
admission/producer concurrency; L2 headless LVGL for affected queue-pressure path.
No actual FreeRTOS worker scheduling or target concurrency/resource confirmation,
no L3 and no waiver. F004 motion deferral and F006 retry policy remain open.
F007 boot/background xTaskCreate allocation failure remains separate C1b work:
failed boot creation can leave the dedicated boot admission outstanding; failed
background creation can leave normal queued requests without a consumer. This
unit does not claim complete allocation/startup coverage or full C1 completion.
Initial-failure mapping under Wi-Fi failure is compiled/source-traced, not an
actual native Wi-Fi worker runtime. Whole-attempt deadline and target budgets remain
explicit gaps. Initial saturation is an injected admission boundary, not a claim
that ordinary UI submits eight pages before boot. Preserve all original C0 evidence.
