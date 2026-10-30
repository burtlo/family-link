---
name: device-firmware-guardrails
description: >-
  Hard rules for changing product firmware under client/device/ and the shared
  libraries it links (client/shared/): sign-in gating, a cheap non-blocking
  LVGL tick, explicit memory placement, worker reuse, no symptom-chasing in
  Wi-Fi or sdkconfig, no spec or status edits, reviewable diffs, and evidence
  before "fixed". Use before implementing, repairing, or debugging any device
  slice, service, worker, view controller, or memory/connection problem.
---

# Device firmware guardrails

Agents working on `client/device/` keep breaking the same rules: services that
run before login, SD and codec I/O on the UI task, buffers that silently land
in internal SRAM, duplicate workers, Wi-Fi tweaks to hide a memory regression,
specs edited to match code, and "fixed" claims with no device evidence. This
skill turns those failures into rules. Follow it literally. When a rule and
your plan disagree, the rule wins or you **stop and ask**.

Normative sources (these win over this skill when they are more specific):

- [`docs/standards/device-presentation-invariants.md`](../../../docs/standards/device-presentation-invariants.md) (PRES-*)
- [`docs/standards/device-target-invariants.md`](../../../docs/standards/device-target-invariants.md) (MEM-*, STK-PATH, PRES-PATH, BOOT-*, claim classes)
- [`docs/standards/client-application-coding-standards.md`](../../../docs/standards/client-application-coding-standards.md)
- [`docs/standards/device-build-flash.md`](../../../docs/standards/device-build-flash.md)
- The active slice plan and memory envelope under `docs/plans/` for the work you were given

Command checklists and the report template: [self-checks.md](self-checks.md).

---

## Step 0 — Before you edit anything

1. Name the **baseline commit**: the last commit where the behavior you are
   touching worked (or the parent of the change under repair). Every diff and
   memory comparison is against it.
2. Name the **scope**: the package, finding, or bug, and the **allowed files**.
   If you were not given a file list, write one and keep to it.
3. Read the code you are about to change **and** the Slice 1/2 reference that
   already solves the same problem (table below). Copy its shape.
4. Read the experience or screen spec the behavior comes from
   (`docs/product/experiences/`, `docs/product/device-ui/`). Code does not
   define behavior.

### Reference implementations to copy

| Problem | Copy this | What it shows |
|---|---|---|
| Service that must not run before login | `client/device/main/product_inbox_service.cpp` | Worker starts only when `user_[0]` is set; `advance` polls a fixed number of results per tick |
| HTTP work, including media slices | `client/device/main/product_inbox_http_worker.cpp` | One worker task, value jobs in, value results out, SHA-256 verified on the worker |
| Codec / speaker writes | `client/device/main/product_settings_worker.cpp` | The only codec owner; volume writes never happen on the LVGL task |
| Login and send HTTP | `product_login_http_worker.cpp`, `product_send_http_worker.cpp` | Existing workers; do not add siblings |
| Tick shape | `client/device/main/product_runtime.cpp` (`ProductRuntime::tick`) | Input, then session work, then present; never returns early |
| Memory probes | `client/device/main/product_mem_probe.cpp` | `product_mem::probe(phase, detail)` logs `V2_MEM` lines when `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC=y` |

---

## The rules

Each rule has MUST / MUST NOT lines and the failure it prevents. "Previously"
lines describe what earlier agents actually did on this codebase.

### G1. Nothing runs before sign-in except login

- MUST: feature services (playback, capture, delivery, media, anything new) do
  no work on a tick when no user is signed in, except one reset on the
  signed-in → signed-out edge.
- MUST NOT: call a feature service's `advance()`/`setSession()`, start its
  worker, touch storage, or touch the codec before sign-in.
- MUST NOT: treat the login session `generation` as a sign-in identity. It
  increments on **every** login stage change (boot → roster → PIN → signed
  in). Use it only as a stale-result tag copied into jobs and compared when
  results return. Reset a service on a **user change** or the sign-out edge,
  never on a generation change.
- Previously: playback, capture, and delivery ran every 50 ms before login,
  and playback "signed out" several times per boot because the generation
  changed, which deleted SD files and poisoned the worker so Play never worked
  after the first real sign-out.

### G2. The LVGL tick is cheap and never blocks

The tick, every `render()`, and every LVGL event callback run on the LVGL task.

- MUST NOT, on that task: `fopen`/`fread`/`fwrite`/`fseek`/`fclose`,
  `std::remove`, `mkdir`, or any call that reaches SD.
- MUST NOT: hash a file, or more than one in-memory slice.
- MUST NOT: call `esp_codec_dev_*` or `bsp_audio_*` (I²C/I²S).
- MUST NOT: take a mutex with `portMAX_DELAY`, or any lock another task holds
  across SD, network, or codec I/O. Use a zero-timeout try-lock and skip work
  this tick, or a lock-free handoff.
- MUST NOT: allocate more than 1 KiB per tick, or allocate per frame at all in
  steady state.
- MUST NOT: re-bind canvases, re-set labels, or invalidate objects when nothing
  changed (PRES-RENDER). Invalidate only when pixels changed.
- MUST NOT: queue unbounded LVGL draw work (for example one `lv_draw_rect` per
  stored point). The LVGL pool is 32 KiB and shared.
- MUST: bound work per tick with fixed loop counts.
- Previously: SD reads in paint and audio pump, SD writes on each downloaded
  slice, whole-file SHA-256 on completion, codec writes at 20 Hz, a
  `portMAX_DELAY` mutex shared with the microphone task's SD writes, and a
  canvas redrawn every tick.

### G3. Memory placement is explicit; no silent internal fallback

Facts about this target (`client/device/sdkconfig.defaults`):

- `CONFIG_SPIRAM_USE_MALLOC=y` with the default 16 KiB
  `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold: **any plain `malloc`/`new` of
  16 KiB or less lands in internal SRAM.**
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`. Wi-Fi, lwIP, TLS, the LCD
  flush, and I²S live on that internal pool. Eating it shows up as Wi-Fi
  association or TLS failures, not as an allocation error in your code.
- Static and global arrays are internal `.bss` unless marked
  `EXT_RAM_BSS_ATTR`. Task stacks from `xTaskCreate` are internal.
- `CONFIG_LV_MEM_SIZE=32768`. Do not raise it (MEM-LEDGER).

Rules:

- MUST: every buffer over 1 KiB states its region and lifetime in your
  report. On target use `heap_caps_malloc(n, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)`
  or `EXT_RAM_BSS_ATTR`. Shared host-buildable code (`client/shared/`) takes an
  injected allocator; it never includes ESP-IDF headers.
- MUST NOT: fall back from PSRAM to internal (a `std::malloc` after a failed
  `heap_caps_malloc`, or a `MALLOC_CAP_INTERNAL` retry). Fail explicitly and
  follow the specified degradation.
- MUST NOT: allocate large resources at boot or in a constructor that runs at
  boot. Allocate on first use; release when the user leaves that mode.
- MUST: a "broker" or "arena" actually frees on release, and mutually
  exclusive modes (Inbox pixels vs Sketch pixels) never coexist.
- Exception: DMA/I²S buffers the driver requires internal. Keep them small and
  list them.
- Previously: renderer planes (2 × 14,400 B) created at boot in internal SRAM
  before Wi-Fi started, static 4 KiB result buffers, internal fallbacks for
  16 KiB HTTP slabs and 28.8 KB / 112.5 KB display surfaces, and surface
  blocks never freed.

### G4. Reuse workers and hardware owners; do not add tasks

- MUST: route HTTP through the existing login, inbox, and send workers, and
  codec writes through the settings worker.
- MUST NOT: add a FreeRTOS task unless the plan you were given explicitly
  allows that task. If you think one is needed, stop and ask.
- MUST NOT: open a second handle to the same hardware or file (speaker codec,
  microphone, `flmsgs.bin`).
- MUST NOT: write a task loop that polls (`vTaskDelay(1)` while idle). Idle
  tasks block on a queue or notification.
- MUST: when you add a job kind to a shared worker, give every result kind a
  consumer, and free result data for kinds a consumer does not own.
- MUST: HTTP range requests are bounded (`bytes=N-M`); never request an open
  range and abort the transfer.
- Previously: a second HTTP task with its own TLS handshake per 16 KiB slice,
  a second speaker codec handle, a 100 Hz polling audio task, and two modules
  opening the same media file.

### G5. Do not fix symptoms in unrelated subsystems

- MUST NOT: change Wi-Fi, lwIP, TLS, partition, or `sdkconfig` settings to make
  a connection, crash, or memory problem go away. These are almost always a
  **consequence** of internal-heap pressure or blocking on the tick.
- MUST: when connecting gets worse after your change, compare `V2_MEM`
  internal free and largest internal block at `ui_ready` between the baseline
  and your build **before** touching anything else.
- A diagnostic-only config flag (for example
  `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC`) may be enabled for a measurement and
  must be reverted before you finish.
- Previously: disabled PMF, changed scan/sort methods, and moved
  `esp_wifi_connect()` instead of finding the boot-time memory regression.

### G6. Do not edit specifications or status to match code

- MUST NOT: edit anything under `docs/product/` (experiences, screens,
  contracts). If code and spec disagree, stop and ask, with the exact rows.
- MUST NOT: change a `Status:` line in `docs/plans/` to "implemented",
  "delivered", or "verified". Only the owner does that after target
  qualification.
- Previously: rewrote Owner and Input rows of a screen spec, and marked slices
  "Implemented… host verified" while the app could not connect.

### G7. Keep diffs reviewable

- MUST NOT: reformat files you are not otherwise changing. If `make format`
  touches lines you did not change, revert those hunks.
- MUST: keep behavior changes and mechanical changes in separate commits.
- MUST: keep CMake lists in their existing layout (one source per line).
- MUST NOT: modify, link to, or cite `poc-v1/`.
- MUST: run `make eol.fix` after editing tracked text (LF only).
- Previously: reformatted Slice 1/2 files in the behavior commit and turned the
  host-test `CMakeLists.txt` into staircase indentation.

### G8. One change, one hypothesis, one proof

- MUST: every fix names the root cause it addresses and the evidence for it
  (log lines, measurement, failing test). "Might help" changes are not fixes.
- MUST NOT: stack speculative fixes. If a change did not fix the symptom on the
  device, revert it before trying the next one, unless it fixed a separate,
  proven defect.
- MUST: add a host test that fails before the fix and passes after, when the
  behavior is reachable on host (`scripts/tests/view_controller_host/`, shared
  model tests).
- MUST: re-run the journeys adjacent to your change (login, mute/unmute,
  sign-out → sign-in, inbox, record/send) to catch regressions.

### G9. Claims match evidence

Use the claim classes from `device-target-invariants.md`:
**tooling** (host checks) → **compile** (`make app:build`) → **flash** →
**boot-marker** (`product_ui_ready build=<id>` in serial) → **behavior**
(operator ran the journey, serial saved).

- MUST NOT: say "fixed", "works", or "verified" above the class you reached.
  `make test` passing is tooling. A successful build is compile.
- MUST: paste command output, not "passed".
- MUST: when the user reports the bug is still present, believe them, collect
  new evidence, and change your hypothesis. Do not re-explain why it should
  work.

---

## Stop and ask

End your turn and ask the owner (with file, function, numbers, and two or three
options) when:

1. A needed change is outside the allowed files.
2. A spec, screen, or contract conflicts with what the code must do.
3. A new task, a new hardware handle, or a second worker seems necessary.
4. Internal free or the largest internal block at `ui_ready` is worse than the
   baseline and you cannot attribute the difference.
5. Something must run on the LVGL task that rule G2 forbids.
6. Two consecutive fixes did not change the device behavior.
7. An exit check fails and you cannot fix it inside the allowed files.

Do not pick the easiest option and continue.

## Finish

Run the self-checks in [self-checks.md](self-checks.md) and fill in the report
template there. Debugging on hardware with the user follows the
[`debug-device`](../debug-device/SKILL.md) skill.
