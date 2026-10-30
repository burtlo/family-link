# Slice 3/4 repair brief — `slice-3-4-grok`

Date: 2026-10-30
Status: **Open** — review findings recorded; no repairs applied
Audience: implementing agent (read this whole document before editing anything)
Scope: commit `67d0325` ("Slice 3 and 4 delivered but the app is broken") on
branch `slice-3-4-grok`, compared with its parent `63c40a9`

---

## 0. How to use this document

This brief is written for an agent that will repair the branch. Previous agents
on this work repeatedly broke the same rules, so this document is deliberately
explicit. Follow it literally.

1. Read sections 1–4 completely before opening any source file for editing.
2. Do the repair packages in section 6 **in order** (R1 → R9). Do not start a
   package until the previous package's exit checks pass.
3. Each package lists **allowed files**. Do not edit files outside that list.
   If you believe another file must change, stop and ask (section 7).
4. Each package lists **exit checks**. Run every one and paste the results into
   your report (section 8). A package is not done because the code compiles.
5. When this document says **STOP AND ASK**, end your turn and ask the owner.
   Do not pick the easiest option and continue.
6. Never claim a slice is "implemented", "delivered", or "verified" in any
   document. Only the owner does that after target qualification.

Shell: Windows PowerShell 5.1. Chain commands with `;`, never `&&` or `||`.
`git grep` works and is the preferred search for the self-checks below.

---

## 1. Hard rules

Each rule is followed by what the previous agent actually did, so you can
recognize the pattern and avoid it.

### 1.1 Nothing runs before sign-in except login

- MUST: playback, capture, delivery, and media work do nothing on a tick when
  no user is signed in, except a one-time reset on the signed-in → signed-out
  edge.
- MUST NOT: call `advance()`, `setSession()`, worker `start()`, storage I/O,
  or codec I/O for these services before sign-in.
- Previous agent: `ProductRuntime::serviceFeatures()` called
  `playback_.setSession`, `playback_.advance`, `delivery_.setSession`, and
  `delivery_.advance` on every tick, signed in or not.
- Model to copy: `InboxService::advance` starts its worker only when
  `user_[0]` is set.

### 1.2 The LVGL tick is cheap and never blocks

The tick (`ProductRuntime::tick`, every `render()`, every LVGL event callback)
runs on the LVGL task. On that task:

- MUST NOT: `fopen`, `fread`, `fwrite`, `fseek`, `fclose`, `std::remove`,
  `mkdir`, or any `MediaStore`/`AttachedStorage` call that reaches SD.
- MUST NOT: hash a file, or hash more than one already-in-memory slice.
- MUST NOT: call `esp_codec_dev_*` or `bsp_audio_*` (I²C/I²S).
- MUST NOT: take a mutex with `portMAX_DELAY`, or any lock that another task
  holds across SD writes.
- MUST NOT: `malloc`/`new` a buffer larger than 1 KiB per tick.
- MUST: bound work per tick (fixed loop counts, as `InboxService::advance`
  does with 4 polls).
- Previous agent: SD reads in `paint()` and `pumpAudio()`, SD writes in
  `apply()`, whole-file SHA-256 in `MediaStore::hashFile`, codec writes every
  tick from `setSession`, and a `portMAX_DELAY` mutex in
  `CaptureService::advance`.

### 1.3 Memory placement is explicit; no silent internal fallback

Facts about this target (`client/device/sdkconfig.defaults`):

- `CONFIG_SPIRAM_USE_MALLOC=y`, with the default
  `SPIRAM_MALLOC_ALWAYSINTERNAL` threshold of 16 KiB. **Any plain
  `malloc`/`new` of 16 KiB or less goes to internal SRAM.**
- `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL=65536`. Wi-Fi, lwIP, the LCD flush,
  and I²S depend on this internal pool.
- Static and global arrays go to internal `.bss` unless marked
  `EXT_RAM_BSS_ATTR`.
- `CONFIG_LV_MEM_SIZE=32768`. The LVGL object pool is 32 KiB and must not be
  increased in this repair.

Rules:

- MUST: every buffer larger than 1 KiB states its region. Use
  `heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT)` on target, or
  `EXT_RAM_BSS_ATTR` for statics.
- MUST NOT: fall back from PSRAM to internal (`std::malloc` after a failed
  `heap_caps_malloc`, or `MALLOC_CAP_INTERNAL` retry). On failure, return
  failure and follow the specified degradation.
- MUST NOT: allocate large resources at boot or in a constructor that runs
  at boot. Allocate on first use, release when the user leaves.
- Exception: DMA and I²S buffers that the driver requires to be internal.
  Keep them small and name them in your report.
- Previous agent: boot-time `RecordSession` renderer planes (2 × 14,400 B
  internal), a static 4 KiB `DeliveryHttpResult`, a static 4 KiB PCM ring,
  an internal 4 KiB task stack, internal fallbacks for 16 KiB HTTP slabs and
  for the 28.8 KB and 112.5 KB display surfaces.

### 1.4 Reuse workers; do not add tasks

- MUST: use the existing workers: `product_login_http_worker`,
  `product_inbox_http_worker`, `product_send_http_worker`, and
  `product_settings_worker`.
- MUST NOT: add a new FreeRTOS task, unless section 6 explicitly allows it.
  The only new task allowed by this repair is the audio output task in R5.
- MUST NOT: create a second handle to the same hardware (speaker codec,
  microphone, SD file).
- Previous agent: a second HTTP task (`play_http`), a second speaker handle,
  and a second owner of `flmsgs.bin`.

### 1.5 Do not fix symptoms in unrelated subsystems

- MUST NOT: edit Wi-Fi, lwIP, partition, or `sdkconfig` settings to make a
  connection problem go away.
- Previous agent: rewrote the Wi-Fi join in `product_network.cpp` (PMF
  disabled, SAE H2E, scan and sort methods, moved `esp_wifi_connect()`)
  instead of finding the memory regression.

### 1.6 Do not edit specifications or status to match code

- MUST NOT: edit anything under `docs/product/` (experiences, screens,
  contracts). If code and spec disagree, STOP AND ASK.
- MUST NOT: change any "Status:" line in `docs/plans/` to claim
  implementation, delivery, or verification.
- Previous agent: rewrote rows of `SCR-MESSAGE-INBOX.md`, and marked slices 3
  and 4 "Implemented… host verified" while the app was broken.

### 1.7 Keep diffs reviewable

- MUST NOT: reformat files you are not otherwise changing.
- MUST: if `make format` changes lines you did not touch, revert those hunks.
- MUST: keep CMake files in their normal indentation.
- Previous agent: reformatted slice 1 and 2 files in the same commit as the
  behavior changes, and turned `scripts/tests/view_controller_host/CMakeLists.txt`
  into staircase indentation.

### 1.8 Repository boundaries

- MUST NOT: modify, link to, or cite paths under `poc-v1/`.
- MUST: run `make eol.fix` after editing tracked text files (LF only).

---

## 2. Intended architecture

### 2.1 Ownership

```text
ProductRuntime (LVGL task)
├─ LoginService            login, PIN, credential, health       [Slice 1]
├─ InboxService            inbox metadata + selection only       [Slice 2]
├─ SettingsService         volume via settings worker            [Slice 2]
├─ MessageTargetService    recipient picker model                [Slice 2]
├─ PlaybackService         selected message play state           [Slice 3]
│    └─ talks to: inbox HTTP worker (media download → SD)
│                 audio output task (SD read → PCM → codec)
├─ CaptureService          record session, created lazily        [Slice 4]
│    └─ talks to: mic task (PCM in), storage writer (SD out)
├─ DeliveryService         send journey + outbox                 [Slice 4]
│    └─ talks to: send HTTP worker
└─ DisplaySurfaceBroker    one pixel footprint, leased by mode

RootViewController
└─ exactly one live child controller at a time
     Login | PinEntry | Cooldown | Inbox | Settings | MessageTarget |
     Sketch | SendFeedback
```

Rules this diagram encodes:

- Services own state that must survive view destruction. Controllers own only
  LVGL objects and borrowed leases.
- Workers never touch LVGL objects and never hold a controller pointer. They
  receive value jobs and return value results.
- The tick only moves intents into queues, moves results out of queues,
  updates small in-memory state, and renders.

### 2.2 Tick contract

`ProductRuntime::tick(now)` must stay in this shape:

```text
pollInput      → buttons, mute edge
login          → flushConnectivity, advance
serviceFeatures:
    inbox      → setSession, advance                         (always; it self-gates)
    if signedIn:
        playback → setSession, advance                       (bounded)
        delivery → setSession, advance                       (bounded)
        capture  → advance only if a capture session exists  (non-blocking)
    else if this is the signed-in → signed-out edge:
        playback.signOut(), delivery.setSession(nullptr, …)  (once)
    settings   → advance
present        → navigation, root.render()
```

### 2.3 Pixel memory, by mode

| Mode | Live buffers | Bytes |
|---|---|---:|
| Login, PIN, Settings, MessageTarget, SendFeedback | none | 0 |
| Inbox, idle | none | 0 |
| Inbox, selected message has visible ink | 1 × 120×120 RGB565 | 28,800 |
| Inbox, reconstruction (only if proven necessary) | +1 × 120×120 RGB565 scratch | 28,800 |
| Sketch | 1 × 240×240 RGB565 | 115,200 |

Inbox and Sketch pixel buffers never exist at the same time. Neighbor cards
own no pixels and no sketch data.

---

## 3. Background facts you will need

- `fl_product_login::SessionWriter::apply_stage` increments `generation_` on
  **every** stage change, including pre-login stages (boot → roster → PIN →
  signed in). `generation` is a stale-result tag, **not** a sign-in identity.
  Never reset a service because the generation changed.
- `product_inbox_http_worker.cpp` already defines a 16 KiB media slice
  (`kMediaSlice`) and verifies SHA-256 incrementally on the worker
  (`note_media_hash`). The incremental hash continues only while **the same
  `operation_id` is used for every slice of one asset** and `range_start`
  equals the bytes already hashed. A new `operation_id` per slice restarts the
  hash.
- `product_inbox_http_worker.cpp::perform_job` cancels `kInboxWireAudio` and
  `kInboxWireSketch` jobs by asking the **legacy** `CarouselSession`
  (`product_inbox::session()` / `InboxWirePlanner::media_job_cancelled`). The
  view-controller runtime does not drive that legacy session. Do not reuse
  those two job kinds for new playback; add a new job kind (R4).
- `InboxService` is the only caller of `product_inbox_http::poll()`. Its
  `consume()` ignores kinds it does not know and frees only `result.parsed`,
  **not** `result.data`. Any new job kind needs a dispatch path, or its
  results are dropped and leaked (R4).
- `RecordSession::reset_runtime()` (called by `boot()`) creates the lab10
  capture and renderer. `lab10_render_config_t` and `lab10_capture_config_t`
  both accept `alloc`, `free`, and `allocator_context`. Today they are left
  null, so plain `malloc` is used.
- `lab10_render_create` allocates `pixels` and `work_pixels`, each
  `LAB10_RENDER_LOGICAL_PIXEL_COUNT` (14,400) bytes, which is under the 16 KiB
  internal threshold.
- `AttachedStorage::store_directory()` writes the directory at offset 0, then
  the identity block at `kIdentityOrigin` (512), then flushes. Media data
  starts at `kMediaDataOrigin` (4096).
- Host tests: `scripts/tests/view_controller_host/` builds one executable,
  `view_controller_test`, with the real pinned LVGL. It is built and run by
  `scripts/tests/test_product_view_controller.py`, which is part of
  `make test`. `make app:test` runs the product host suite from
  `config/app-test-suite.yaml`. `make app:build` builds the firmware.

---

## 4. Findings

Severity: **P0** breaks boot, connection, or a core function. **P1** violates
the memory envelope or slice requirements and will fail on target. **P2** is a
correctness, durability, or process problem.

### F1 (P0) — Boot-time construction consumes internal SRAM before Wi-Fi

Where:

- `client/device/main/product_app_run.cpp::attach_product_ui()` constructs
  `ProductRuntime` while holding the display lock, before
  `product_network::start()`.
- `client/device/main/product_runtime.cpp` constructor calls
  `capture_.open(BSP_SD_MOUNT_POINT "/flmsgs.bin")`.
- `client/device/main/product_capture_service.cpp::CaptureService::open` calls
  `session_.boot(0)` → `RecordSession::reset_runtime()` → lab10 capture and
  renderer creation with plain `malloc`.

Effect: about 28 KB of internal SRAM (two 14,400 B renderer planes) is taken
before Wi-Fi associates and is never released. The legacy record bridge
created `RecordSession` lazily (`ensure_session()`, with memory probes), only
when recording began.

Other internal consumers added by the branch:

| Allocation | Location | Why internal |
|---|---|---|
| `static DeliveryHttpResult copy` (4 KiB `body`) | lambda in the `ProductRuntime` constructor | static `.bss` |
| `uint8_t ring[4096]`, `PlaybackFetchResult polled` | `product_playback_worker.cpp` | static `.bss` |
| `play_pcm` stack 4 KiB | `product_playback_worker.cpp::workerStart` | `xTaskCreate` default |
| 16 KiB response slab | `product_playback_worker.cpp::alloc` | falls back to `std::malloc` |
| `InkCache` copies, up to 3 × 64 KiB | `PlaybackService::loadCache` | plain `malloc`; ≤16 KiB is internal |
| Inbox 28.8 KB and Sketch 112.5 KB surfaces | `product_runtime.cpp::allocateSurface` | PSRAM request silently retries internal |

The surface fallback decides `MEM-OPEN-003` (surface region and fallback)
without the measurement the memory envelope requires.

This is the leading hypothesis for the connection failures. It is unmeasured
because no device logs exist; R3's exit check measures it.

### F2 (P0) — Out-of-scope Wi-Fi changes

`client/device/main/product_network.cpp` differs from `63c40a9`:

- `esp_wifi_connect()` removed from the `WIFI_EVENT_STA_START` handler and
  called directly after `esp_wifi_start()`;
- `cfg.sta_disconnected_pm = false` (redundant: `sdkconfig.defaults` already
  has `CONFIG_ESP_WIFI_STA_DISCONNECTED_PM_ENABLE=n`);
- `pmf_cfg.capable = true` removed (PMF disabled);
- `sae_pwe_h2e`, `WIFI_ALL_CHANNEL_SCAN`, and `WIFI_CONNECT_AP_BY_SIGNAL`
  added;
- disconnect log format changed.

Not in scope, weakens security, does not address heap pressure.

### F3 (P0) — Playback resets on generation changes and runs before sign-in

Where: `ProductRuntime::serviceFeatures()` and
`PlaybackService::setSession()`.

```cpp
if (generation_ != 0 && generation != generation_)
    signOut();
```

Because the generation changes on every pre-login stage, `signOut()` runs
several times before anyone logs in. Each call:

- calls `transport_.request_quiesce()` (see F4);
- calls `media_.discardSession()`, which runs `std::remove()` on SD files from
  the LVGL task;
- clears progress, caches, and the bearer.

`setSession` also calls `audio_.set_volume(...)` every tick. Once the speaker
has been opened, that is two codec I²C writes from the LVGL task at 20 Hz.

### F4 (P0) — Playback is permanently dead after the first sign-out

Where: `product_playback_worker.cpp`.

- `workerQuiesce()` sets `quiesce_requested = true`.
- Only the first `workerStart()` clears it, and later calls return early
  because `http_started` is true.
- `workerSubmit()` returns false while `quiesce_requested` is true.

User-visible result: after a mute or a different user signs in, pressing Play
switches the button to "Pause" and nothing plays. `toggle()` sets
`buffering_`, every `submit()` fails, and `advance()` retries forever. Only a
reboot recovers.

### F5 (P0) — Capture can block the LVGL tick on SD writes

Where: `product_capture_service.cpp`.

- `CaptureService::Lock` takes a recursive mutex with `portMAX_DELAY`.
- `advance()`, `consumeNotice()`, `routeTouch()`, `render()`, and
  `blocksInboxFetch()` take it on the LVGL task.
  `blocksInboxFetch()` is called twice per tick.
- `pushPcm()` takes it on the microphone task, and `RecordSession::push_pcm`
  appends to SD while holding it.

Effect: any SD write latency on the microphone path stalls the UI.

### F6 (P1) — A duplicate HTTP stack for playback

Where: `product_playback_worker.cpp` (`play_http` task, 12 KiB stack, own
queues).

- Duplicates the inbox HTTP worker, which already handles media slices with
  streaming SHA-256.
- Up to four HTTP clients can now be live at once: login, inbox, playback,
  send.
- Every 16 KiB slice calls `esp_http_client_init`, which means a new
  connection and, on HTTPS, a new TLS handshake.
- Requests send `Range: bytes=N-` (open-ended) and then abort with
  `ESP_FAIL` once the slab is full. The server streams the remainder, and the
  connection is torn down every time.

### F7 (P1) — Audio task polling and two codec owners

- `audioRun` runs at priority 8 and loops forever with `vTaskDelay(1)`, every
  10 ms, even with nothing playing.
- `bsp_audio_codec_speaker_init()` is called in both
  `product_settings_worker.cpp` and `product_playback_worker.cpp`. Two
  handles write volume and mute independently. Slice 2 made the settings
  worker the only codec writer.

### F8 (P1) — Storage I/O on the LVGL task

All of these run on the LVGL task:

- `MediaStore::read` / `append`: `fopen` → `fseek` → `fread`/`fwrite` →
  `fclose` on every call.
- `PlaybackService::pumpAudio`: up to four 512 B SD reads per tick.
- `PlaybackService::paint`: loops over 4 KiB SD reads.
- `PlaybackService::loadCache`: up to 64 KiB of synchronous reads.
- `PlaybackService::apply` → `MediaStore::append`: writes each 16 KiB slice.
- `MediaStore::hashFile`: software SHA-256 of the entire audio file on
  completion. This can trip the task watchdog on long messages.
- `MediaStore::begin` → `makeDir` and `MediaStore::discardSession` →
  `std::remove`.

### F9 (P1) — Neighbor cards hold and draw sketch data

Where: `PlaybackService::noteVisible`, `PlaybackService::loadCache`,
`PlaybackService::visitInk`, `InboxViewController::onInk`.

- Whole sketch streams are copied into heap for up to three visible cards.
- `onInk` draws neighbor ink inside `LV_EVENT_DRAW_MAIN` with one
  `lv_draw_rect` **per stored point**. Each call queues a draw task allocated
  from the 32 KiB LVGL pool, so a sketch with thousands of points can exhaust
  it.
- Slice 3 says neighbors use the default card presentation unless the owner
  approves raster previews.

### F10 (P1) — The canvas is re-bound and invalidated every tick

Where: `InboxViewController::presentInk()`, called from every `render()`.

- `playback_.paint()` runs every tick (SD reads; see F8).
- `lv_canvas_set_buffer(...)` and `lv_obj_invalidate(...)` run every tick for
  the selected card whether or not anything changed, so the card redraws
  120×120 at 20 fps.
- `lv_image_set_src(card.canvas, nullptr)` runs every tick for non-ink cards.

### F11 (P1) — The surface broker never frees inbox surfaces

Where: `DisplaySurfaceBroker::release()` in `product_display_surface.cpp`.

- Only the record slot frees its pixels on release. `live_` and `scratch_`
  (28.8 KB each) stay allocated for the rest of the boot.
- `acquireRecord()` allocates 112.5 KB even while inbox blocks still exist.
- The memory envelope describes one footprint used in mutually exclusive
  modes.

### F12 (P2) — Identity is written outside the atomic directory commit

Where: `client/shared/record_capture/fl_product_record/storage.cpp`.

- `store_directory()` writes the directory, then `write_identity_block()`,
  then flushes.
- Power loss between the two writes leaves a committed finished slot with no
  identity. `DeliveryService::startPayload()` skips slots without a valid
  identity, so that slot is never sent and never cleared. Only
  `kAttachedFinishedSlots = 2` exist.
- The identity block has no checksum. A torn write can corrupt identities of
  already-committed slots.

### F13 (P2) — Two owners of `flmsgs.bin`

- `product_record_bridge.cpp::ensure_media()` opens
  `BSP_SD_MOUNT_POINT "/flmsgs.bin"` with its own `FileMediaBackend` and its
  own `RecordSession`.
- `CaptureService` opens the same file.
- Microphone PCM reaches `CaptureService` only through
  `product_record::set_pcm_observer`, a redirect added inside the legacy
  `push_pcm`.

### F14 (P2) — File-scope renderer state

`product_capture_service.cpp` stores `s_paint` and `s_canvas` as file-scope
statics for the lab10 render hooks instead of binding them to the instance and
lease.

### F15 (P2) — Record-install failure silently discards the journey

`ProductRuntime::install()`: if the Sketch controller cannot be installed (for
example, the 112.5 KB lease fails), the runtime applies `kSendIntentBoot` and
swaps to Inbox without animation. The user gets no feedback.

### F16 (P2) — Documentation and process

- Status lines in `device-ui-view-controller-third-slice-playback.md`,
  `device-ui-view-controller-fourth-slice-record-send.md`, and
  `device-ui-remaining-slices.md` were changed to claim implementation and
  host verification.
- `docs/product/device-ui/screens/SCR-MESSAGE-INBOX.md` Owner and Input rows
  were rewritten by the implementation agent.
- Slice 1 and 2 files were mechanically reformatted in the behavior commit:
  `product_view_controller.cpp`, `product_inbox_service.cpp/.hpp`,
  `product_message_target_service.cpp`, `product_runtime.cpp/.hpp`,
  `product_message_target_view_controller.cpp`.
- No per-package verification or target measurements were done (S3-P5,
  S4-P6).

### F17 (P2) — Host test CMake file mangled

`scripts/tests/view_controller_host/CMakeLists.txt` was rewritten with
cascading staircase indentation, and test sources are interleaved with product
sources on the same lines (`... product_playback_service.cpp" playback_service_test.cpp`).

---

## 5. What to keep

Do not throw these away:

- The service/controller split: `DeliveryService` and `CaptureService` own
  capture and upload state; `SketchViewController` and
  `SendFeedbackViewController` only borrow snapshots and leases.
- `SendFeedbackViewController` as written.
- `SketchViewController`'s lease order: acquire in `createView`, detach the
  renderer and release in `viewWillUnload`.
- `SendIdentity` and per-slot identity persistence, once F12 is fixed.
- The `flsk2` replay rules in `PlaybackService::paint`: one stored point is
  one pixel, background-colored points are painted, there is no bridging, and
  timestamps are validated. Keep the logic; change only where the bytes come
  from (R6).
- The existing host tests `playback_service_test.cpp` and
  `record_send_test.cpp`. Update them for the new design; do not delete
  coverage.

---

## 6. Repair packages

### R1 — Revert the Wi-Fi changes (F2)

Allowed files: `client/device/main/product_network.cpp`.

Steps:

1. Run `git checkout 63c40a9 -- client/device/main/product_network.cpp`.
2. Do not make any other change to this file in this repair.

Exit checks:

- `git diff 63c40a9 -- client/device/main/product_network.cpp` prints nothing.
- `make app:build` succeeds.

If connection is still unreliable after R2 and R3 are complete, report it
with heap numbers. Do not edit Wi-Fi code yourself.

---

### R2 — Gate the new services on sign-in; fix the session reset (F3, F4, F5)

Allowed files:

- `client/device/main/product_runtime.cpp`, `product_runtime.hpp`
- `client/device/main/product_playback_service.cpp`, `.hpp`
- `client/device/main/product_playback_worker.cpp` (F4 only; R4 deletes its
  HTTP half)
- `client/device/main/product_capture_service.cpp`, `.hpp`
- `client/device/main/product_delivery_service.cpp`, `.hpp`
- `scripts/tests/view_controller_host/*_test.cpp`

Steps:

1. **Gate in `serviceFeatures()`.** Restructure it to the shape in section
   2.2. Add a `bool was_signed_in_ = false;` member to `ProductRuntime`. The
   code below shows the shape; adapt it to the real signatures:

   ```cpp
   if (signedIn) {
       playback_.setSession(session.signed_user(), session.generation(),
                            session.credential_token(), playback_allowed,
                            settings_.codecVolume(), now_);
       playback_.advance(now_);
       delivery_.setSession(session.signed_user(), session.credential_token(),
                            service_.wifiUp(), refreshFailed, session.generation(), now_);
       delivery_.advance(now_);
   } else if (was_signed_in_) {
       playback_.signOut();
       delivery_.setSession(nullptr, "", service_.wifiUp(), false, session.generation(), now_);
   }
   was_signed_in_ = signedIn;
   ```

   - Keep the existing mute path in `pollInput()`; it already signs services
     out.
   - Remove the duplicate `inbox_.setFetchHeld(...)` call; one call per tick
     is enough.
   - Move `delivery_.noteSeed(esp_random())` so it runs once, not every tick
     while Wi-Fi is up.

2. **Reset playback on user change, not generation change.**
   - Add the signed-in user to `PlaybackService::setSession` and keep a
     `user_[24]` member.
   - Call `signOut()` only when a non-empty `user_` differs from the new
     user.
   - Keep `generation_` purely as a tag that is copied into each submitted
     job and compared when results are applied. A mismatched result is
     dropped; it does not reset the service.

3. **Apply volume only on change.** Keep `int applied_volume_ = -1;` and call
   `audio_.set_volume` only when `codec_volume != applied_volume_`. R5 moves
   the actual codec write off the LVGL task; for now, at least stop the 20 Hz
   writes.

4. **Make `signOut()` storage-free on the tick.** `media_.discardSession()`
   must not run `std::remove` on the LVGL task. For R2, record the IDs to
   discard and let R4's worker delete them. If that is too large for R2, skip
   the removal and leave a short `TODO(R4)` naming the worker job that will do
   it.

5. **Fix F4.** Make quiescence per session. Either clear
   `quiesce_requested` and `quiescent` at the start of each signed-in session
   (add a `begin_session` entry to `PlaybackTransport`), or remove the
   quiesce gate from `workerSubmit` and rely on generation tags. R4 deletes
   this worker, but the test in step 7 must survive R4.

6. **Make capture non-blocking on the LVGL task.**
   - `CaptureService::advance()` returns immediately if no capture session
     exists (see R3: the session becomes lazy).
   - On the LVGL task, use a try-lock with a zero timeout
     (`xSemaphoreTakeRecursive(lock, 0)`). If the lock is busy, skip the work
     this tick. This applies to `advance`, `consumeNotice`, `render`,
     `blocksInboxFetch`, and `inputOpen`/`wiping`/`recording`/`reversing`.
   - `routeTouch` must not drop touch points silently. If the lock is busy,
     push the point into a small fixed queue (for example 32 entries, owned by
     `CaptureService`) and drain it on the next tick.
   - Record in your report that the microphone path still writes SD under the
     lock. R8 removes that.

7. **Add host tests** in `scripts/tests/view_controller_host/`:
   - `pre_login_ticks_do_no_playback_or_delivery_work`: drive
     `ProductRuntime::tick` through boot → roster → PIN with fake transports
     that count calls. Assert zero playback `submit`/`start`, zero delivery
     `start`/`submit`, zero audio `set_volume`, and zero `MediaStore` file
     operations.
   - `generation_change_does_not_reset_playback`: sign in, start playing,
     change the generation without changing the user, and assert progress and
     selection are kept.
   - `playback_works_after_sign_out_and_sign_in`: sign in → Play (submit
     observed) → mute → unmute → sign in → Play, and assert a fetch is
     submitted the second time.
   - `capture_advance_does_not_block_when_locked`: hold the capture lock from
     the test and assert `advance()` returns.

Exit checks:

- `make test` passes, including the four new tests.
- `make app:build` succeeds.
- Searching `product_runtime.cpp` for the call sites of `playback_.advance`
  and `delivery_.advance` shows them only inside the `if (signedIn)` block.

---

### R3 — Lazy, PSRAM-placed recording resources (F1)

Allowed files:

- `client/device/main/product_runtime.cpp`, `.hpp`
- `client/device/main/product_capture_service.cpp`, `.hpp`
- `client/device/main/product_delivery_service.cpp`, `.hpp`
- `client/shared/record_capture/fl_product_record/session.cpp` and
  `include/fl_product_record_session.hpp`
- `client/shared/record_capture/tests/*` (only to keep model tests passing)
- `scripts/tests/view_controller_host/*_test.cpp`

Steps:

1. **Remove boot-time open.** The `ProductRuntime` constructor must not call
   `capture_.open(...)`, must not create a `RecordSession` renderer, and must
   not touch SD. It may store the media path string.

2. **Split storage from the renderer.**
   - Committed-send resume needs the finished-slot directory after sign-in.
     Opening the directory (`FileMediaBackend::open_path` +
     `AttachedStorage::attach_media` + `load_directory`) may happen on the
     first signed-in tick. It must run off the LVGL task if it reads more
     than the 4 KiB directory region. If it can only run on the LVGL task,
     STOP AND ASK.
   - The lab10 capture and renderer must be created only when a recording
     journey begins (`DeliveryService::beginWipe` → `CaptureService::begin`),
     and destroyed (`lab10_render_destroy`, `lab10_capture_destroy`) when the
     take finishes, is cancelled, or is discarded.
   - In shared code, change `RecordSession::reset_runtime()` so it does not
     create the capture and renderer. Add explicit
     `bool RecordSession::ensure_capture()` and
     `void RecordSession::release_capture()` methods. Keep shared code
     host-buildable: no ESP-IDF headers in `client/shared/`.

3. **Inject the allocator.**
   - Add `RecordSession::set_allocator(void *(*alloc)(size_t, void *),
     void (*free)(void *, void *), void *context)`. Pass it into both
     `lab10_capture_config_t` and `lab10_render_config_t` (`alloc`, `free`,
     `allocator_context`).
   - On target, `ProductRuntime` passes a PSRAM allocator:

     ```cpp
     void *psramAlloc(size_t bytes, void *)
     {
         return heap_caps_malloc(bytes, MALLOC_CAP_SPIRAM | MALLOC_CAP_8BIT);
     }
     void psramFree(void *block, void *)
     {
         heap_caps_free(block);
     }
     ```

     There is no internal fallback. If the allocation fails,
     `CaptureService::begin` returns false, and the existing
     `kSendIntentCaptureFailed` path produces the "couldn't record" outcome.
   - On host, pass nothing; plain `malloc` is fine there.

4. **Remove the static `DeliveryHttpResult copy`.** Do not keep a 4 KiB
   static in a lambda. Either:
   - have the send worker's poll return a pointer to its own PSRAM-allocated
     result, or
   - make the copy a `DeliveryService` member that lives in PSRAM because
     `ProductRuntime` is heap-allocated. Check this: `ProductRuntime` is
     created with `new` and is larger than 16 KiB, so it lands in PSRAM. Note
     the measured `sizeof(ProductRuntime)` in your report.

5. **Remove the silent surface fallback** in
   `product_runtime.cpp::allocateSurface`. A PSRAM request returns PSRAM or
   `nullptr`; it never retries internal. Leave `MEM-OPEN-003` open.

6. **Tests:**
   - `runtime_construction_allocates_no_capture`: construct `ProductRuntime`
     in the host test with a counting allocator injected, and assert the
     capture/renderer allocator is called zero times.
   - `capture_resources_released_after_finish_and_discard`: begin a take,
     finish it, and assert the allocator's live-bytes count returns to zero.
     Do the same for discard and cancel.

Exit checks:

- `make test` and `make app:build` pass.
- `git grep -n "capture_.open" -- client/device/main/product_runtime.cpp`
  shows no call in the constructor.
- **Target measurement.** Flash `63c40a9`, save the monitor log, flash the
  repaired build, save the monitor log. From each `product_mem::probe("ui_ready")`
  line and the first probe after Wi-Fi association, report: internal free,
  internal largest free block, DMA-capable largest block, and PSRAM free. The
  repaired build must be no worse than `63c40a9` for internal free and
  internal largest block. If it is worse, STOP AND ASK with the numbers.

---

### R4 — Media downloads through the inbox HTTP worker (F6, most of F8)

Allowed files:

- `client/device/main/product_inbox_http_worker.cpp`, `.hpp`
- `client/device/main/product_inbox_service.cpp`, `.hpp` (dispatch only)
- `client/device/main/product_playback_service.cpp`, `.hpp`
- `client/device/main/product_playback_worker.cpp`, `.hpp` (delete the HTTP
  half)
- `client/device/main/product_media_store.cpp`, `.hpp`
- `client/device/main/product_runtime.cpp`
- `client/device/main/CMakeLists.txt` (only if a file is removed)
- `scripts/tests/view_controller_host/*`

Design you must implement:

1. **New job kind.** Add `constexpr int kPlaybackMediaJob = 6;` (or the next
   free value) to `product_inbox_http_worker.hpp`. Add an asset field
   (`audio` or `sketch`) and a destination file path field
   (`char file_path[96]`) to `Job`. Do not use `kInboxWireAudio` or
   `kInboxWireSketch`: those consult the legacy `CarouselSession` and would
   be cancelled.

2. **Bounded ranges.** `PlaybackService` submits
   `Range: bytes=N-M` with `M = min(N + 16383, asset_bytes - 1)`. Never send an
   open-ended range.

3. **One `operation_id` per asset.** Every slice of one asset uses the same
   `operation_id`, with `range_start` equal to the bytes already stored. This
   keeps the worker's incremental SHA-256 (`note_media_hash`) working across
   slices. A new download of the same asset after a failure uses a new
   `operation_id` and starts from offset 0, or from the stored offset with a
   restarted hash. Pick one approach and document it in a header comment.

4. **The worker writes SD, not the tick.**
   - For `kPlaybackMediaJob`, the worker opens `file_path` at `range_start`
     (`"r+b"`, or `"wb"` when `range_start == 0`), writes each
     `HTTP_EVENT_ON_DATA` chunk directly to the file, and updates the hash
     with the same bytes. No 16 KiB slab is allocated for these jobs.
   - The worker `fflush`es and `fclose`s before posting the result.
   - The result carries no `data`. It carries `bytes_written`,
     `full_asset`, `hash_matches`, and status.
   - On a hash mismatch for a full asset, the worker deletes the file and
     reports `hash_matches = 0`.

5. **Dispatch results.** `InboxService` remains the only poller. Add a
   forwarding hook so it hands `kPlaybackMediaJob` results to
   `PlaybackService`, for example
   `void setMediaSink(void (*sink)(void *, const product_inbox_http::Result &), void *context)`.
   `ProductRuntime` wires it. `InboxService::consume` must also free
   `result.data` for any kind it does not own, so nothing leaks.

6. **`MediaStore` becomes tick-safe metadata.** On the tick, `MediaStore`
   tracks only per-asset state (`stored`, `total`, `complete`, `failed`,
   `sha`, file path) in memory. Remove from tick-reachable code:
   `append`, `hashFile`, `read`, `readAudio`, `readSketch`, `makeDir` calls,
   and `std::remove`. Directory creation and file deletion become worker
   jobs (for example `kPlaybackMediaMaintenanceJob`) or run once at sign-in
   on the worker.

7. **Delete the `play_http` task**, its queues, `perform`, `onHttp`,
   `joinUrl`, `alloc`, and the `workerSubmit`/`workerPoll`/`workerRelease`/
   `workerQuiesce` functions from `product_playback_worker.cpp`. Keep only the
   audio half for R5.

8. **Inbox fetch fairness.** One worker now serves metadata and media. Do not
   queue more than one playback slice at a time, so a metadata refresh waits
   for at most one 16 KiB slice. Note the observed refresh latency in your
   report. If metadata refresh latency becomes visible to the user, STOP AND
   ASK. Do not add a second worker.

9. **Tests:**
   - `playback_media_uses_bounded_ranges_and_one_operation_per_asset`
   - `playback_results_are_dispatched_not_dropped` (with the fake inbox
     transport returning a `kPlaybackMediaJob` result)
   - `unknown_result_kinds_free_data` (a counting free)
   - `tick_reaches_no_file_io`: a fake file layer that fails the test if
     called during `ProductRuntime::tick`.

Exit checks:

- `git grep -n "play_http" -- client/device/main` prints nothing.
- `git grep -nE "fopen|fread|fwrite|fseek|std::remove|hashFile" -- client/device/main/product_playback_service.cpp client/device/main/product_inbox_view_controller.cpp client/device/main/product_runtime.cpp`
  prints nothing.
- `git grep -n "xTaskCreate" -- client/device/main` lists no task that did
  not exist at `63c40a9`, except `play_pcm` (removed or rewritten in R5).
- `make test` and `make app:build` pass.

---

### R5 — Audio output on a worker with one codec owner (F7, the rest of F8)

Allowed files:

- `client/device/main/product_playback_worker.cpp`, `.hpp`
- `client/device/main/product_playback_service.cpp`, `.hpp`
- `client/device/main/product_settings_worker.cpp`
- `client/device/main/product_settings_service.cpp`, `.hpp`
- `client/device/main/product_runtime.cpp`
- `scripts/tests/view_controller_host/*`

Design:

1. **One codec owner.** Exactly one module calls
   `bsp_audio_codec_speaker_init()`. Make it a small audio-output owner used by
   both the settings worker (volume) and playback (PCM), or keep it in the
   settings worker and expose `speaker()` to the audio task. Volume and mute
   writes happen only on the settings worker or the audio task, never on the
   LVGL task.

2. **The audio task does the SD reads.**
   - The tick sends value commands to the audio task's queue:
     `Play{file_path, start_offset, available_bytes, total_bytes, generation}`,
     `MoreAvailable{available_bytes}`, `Pause`, `Stop`.
   - The audio task opens the file once per Play, reads 512–1024 B chunks with
     a reused stack or PSRAM buffer, and writes them to the codec. It reads
     only below `available_bytes`; the tick updates that from R4 results after
     the HTTP worker has closed the file.
   - The sample clock (`written_samples`, read with a critical section) stays
     the playback clock. Sketch timing is derived from it, never from the UI
     tick.

3. **No polling.** While idle, the audio task blocks on its queue with
   `portMAX_DELAY`. While playing, it blocks in `esp_codec_dev_write`. Remove
   `vTaskDelay(1)`.

4. **Task placement.** Create the audio task lazily on first Play. If its
   stack can be PSRAM-backed safely (`xTaskCreateWithCaps` with
   `MALLOC_CAP_SPIRAM`), use that. If it must be internal for I²S timing,
   keep it at 4 KiB or less and say so in the report.

5. **`PlaybackService` loses `pumpAudio`'s file reads.** `pumpAudio` becomes
   "send `MoreAvailable` when the stored byte count grows" and "read the
   clock".

6. **Tests:** with a fake audio endpoint, assert the tick never calls
   `set_volume` more than once per volume change, and that Play, Pause, Stop,
   and MoreAvailable are sent as value commands.

Exit checks:

- `git grep -n "bsp_audio_codec_speaker_init" -- client/device/main` shows
  exactly one call site.
- `git grep -n "vTaskDelay(1)" -- client/device/main/product_playback_worker.cpp`
  prints nothing.
- `make test` and `make app:build` pass.

---

### R6 — Inbox ink presentation (F9, F10)

Allowed files:

- `client/device/main/product_inbox_view_controller.cpp`, `.hpp`
- `client/device/main/product_playback_service.cpp`, `.hpp`
- `client/device/main/product_inbox_http_worker.cpp` (sketch load job only)
- `scripts/tests/view_controller_host/*`

Steps:

1. **Delete neighbor ink.** Remove `InkCache`, `caches_`, `loadCache`,
   `clearCaches`, `noteVisible`, `visitInk`, the `card.ink` object, and the
   `onInk` draw callback. Neighbor cards always show the default portrait,
   name, and duration.

2. **Check the spec conflict before finishing step 1.** Slice 3 also says
   "Releasing a surface must not silently revert a visible paused card to its
   portrait." With three live cards, a paused neighbor that still has ink
   would revert to its portrait under step 1. STOP AND ASK the owner which
   behavior they want for a *visible, non-selected* paused card. Do not solve
   it with per-card buffers, per-point drawing, or cached stroke copies.

3. **Selected-card sketch bytes come from memory.**
   - When the selected message's sketch asset is complete, submit a worker
     job that reads the sketch file into **one** PSRAM buffer (cap it at the
     contract's maximum sketch size and reject anything larger). Only the
     selected message has this buffer. Free it on selection change, sign-out,
     and Inbox unload.
   - This is stroke data, not pixels, and there is exactly one. Note in your
     report that it is an engineering choice under `MEM-OPEN-005`/`006`.
   - `PlaybackService::paint` keeps its `flsk2` rules but reads from that
     buffer instead of calling `media_.readSketch`.

4. **Bind once, invalidate on change.**
   - Call `lv_canvas_set_buffer` once, when the lease is acquired or the
     selected card slot changes.
   - Make `paint()` return the number of new points painted. Call
     `lv_obj_invalidate` only when that is greater than 0, or when the frame
     was reset.
   - Call `lv_image_set_src(nullptr)` only on the transition from ink to no
     ink, not every tick.

5. **Lease lifetime.** Release the inbox lease when the selected message has
   no visible ink, when the selection changes, and in `viewWillUnload`.
   Detach the canvas source (`lv_image_set_src(canvas, nullptr)`) before
   every release.

6. **Tests (real LVGL host):**
   - `neighbor_cards_never_draw_ink`
   - `canvas_bound_once_per_lease`
   - `invalidate_only_when_points_painted`
   - `lvgl_pool_flat_during_playback`: use `lv_mem_monitor` before and after
     200 ticks of playing a 5,000-point sketch, and assert used bytes and the
     largest free block are unchanged within a small tolerance.

Exit checks:

- `git grep -nE "InkCache|visitInk|onInk|noteVisible" -- client/device/main`
  prints nothing.
- `make test` and `make app:build` pass.

---

### R7 — One pixel footprint (F11, F14)

Allowed files:

- `client/device/main/product_display_surface.cpp`, `.hpp`
- `client/device/main/product_capture_service.cpp`, `.hpp`
- `client/device/main/product_sketch_view_controller.cpp`, `.hpp`
- `client/device/main/product_inbox_view_controller.cpp`
- `scripts/tests/view_controller_host/*`

Steps:

1. `DisplaySurfaceBroker::release()` frees the pixels for **every** slot,
   not only `record_`.
2. `acquireRecord()` returns false if `live_` or `scratch_` is held.
   `acquire()` and `acquireScratch()` return false if `record_` is held.
3. Verify the transition order in `product_transition_coordinator.cpp`. The
   outgoing controller must unload, and return its lease, before the incoming
   controller's `createView` runs. Read the code and write a test; do not
   assume. If the coordinator builds the incoming controller first, STOP AND
   ASK. Do not change the coordinator without approval.
4. Replace the `s_paint` and `s_canvas` file statics in
   `product_capture_service.cpp` with members passed as the hook `context`.
   `detachSurface()` must clear the hooks before the lease is released.
5. **Tests:**
   - `inbox_and_record_pixels_never_coexist` (Inbox with ink → MessageTarget
     → Sketch; assert the allocator's live bytes never exceed 115,200 plus
     any declared transition snapshot)
   - `release_frees_every_slot`
   - `record_refused_while_inbox_lease_held`

Exit checks: `make test` and `make app:build` pass.

---

### R8 — Durable identity, one media-file owner, record failure feedback (F12, F13, F15)

Allowed files:

- `client/shared/record_capture/fl_product_record/storage.cpp`,
  `include/fl_product_record_storage.hpp`
- `client/shared/record_capture/tests/*`
- `client/device/main/product_record_bridge.cpp`, `product_record.hpp`
- `client/device/main/product_mic.*`
- `client/device/main/product_capture_service.cpp`, `.hpp`
- `client/device/main/product_runtime.cpp`
- `client/device/main/CMakeLists.txt`
- `scripts/tests/view_controller_host/*`

Steps:

1. **Identity durability.**
   - Add a CRC32 to each identity slot.
   - Write the identity block **before** the directory. The directory write
     plus flush remains the single commit point. An identity is trusted only
     if its CRC is valid and its `client_message_id` matches the committed
     directory slot (`read_identity_block` already checks the match).
   - Decide what happens to a committed slot whose identity is missing or
     invalid. Read `docs/product/experiences/message-recording-and-sending.md`
     and `docs/product/communication-contracts/message-delivery.md`. If they
     specify it, implement it. If they do not, STOP AND ASK. Do not leave the
     slot stuck forever.
   - Add model tests for a torn identity write, a torn directory write, an
     identity CRC mismatch, and restart at each boundary.

2. **One owner of `flmsgs.bin`.**
   - Run `git grep -n "product_record::" -- client/device/main` and list
     every caller in your report.
   - Route microphone PCM straight to `CaptureService` through a narrow sink
     registered by `ProductRuntime` (for example
     `product_mic::set_sink(push, context)`). Remove `set_pcm_observer` and
     the redirect in `push_pcm`.
   - Remove `ensure_media()`'s ability to open `flmsgs.bin`, or remove the
     legacy record bridge from the build if nothing else needs it. If other
     legacy bridges still depend on it, STOP AND ASK with the list.

3. **Move SD writes off the capture lock.** The microphone task appends PCM
   to a fixed ring owned by `CaptureService`, placed in PSRAM and sized from
   LAB-16 evidence (state the size). A storage writer drains the ring to
   `AttachedStorage` without holding the lock the LVGL task uses. If this
   needs a new task, STOP AND ASK. Prefer draining on an existing worker.
   Ring overflow is an explicit capture outcome, not silent data loss.

4. **Record-install failure feedback.** Find the specified outcome for
   "couldn't record" or resource failure in the recording experience and
   make `ProductRuntime::install()` follow it instead of silently swapping
   to Inbox. If unspecified, STOP AND ASK.

Exit checks:

- `git grep -n "flmsgs.bin" -- client/device/main` shows one owner.
- `git grep -n "set_pcm_observer" -- client/device/main` prints nothing.
- Model tests and `make test` pass; `make app:build` succeeds.

---

### R9 — Documentation, formatting, and CMake cleanup (F16, F17)

Allowed files:

- `docs/plans/v2-experience/device-ui-view-controller-third-slice-playback.md`
  (status line only)
- `docs/plans/v2-experience/device-ui-view-controller-fourth-slice-record-send.md`
  (status line only)
- `docs/plans/v2-experience/device-ui-remaining-slices.md` (status line only)
- `docs/product/device-ui/screens/SCR-MESSAGE-INBOX.md` (revert only)
- `scripts/tests/view_controller_host/CMakeLists.txt`
- slice 1 and 2 files listed in F16 (reformat revert only)

Steps:

1. Set the three status lines to
   `Status: Implementation in repair; target qualification open.` Change
   nothing else in those documents.
2. `git checkout 63c40a9 -- docs/product/device-ui/screens/SCR-MESSAGE-INBOX.md`.
   If the view-controller runtime makes its Owner, Input, or Background rows
   wrong, write the proposed wording in your report for the owner. Do not
   edit the spec.
3. Rewrite `scripts/tests/view_controller_host/CMakeLists.txt` with normal
   indentation: one source per line, test sources grouped first, then product
   sources, then shared sources.
4. For each slice 1/2 file in F16, revert hunks that are formatting-only.
   Keep hunks that carry behavior needed by R2–R8.
5. Run `make eol.fix`, then `make format.check`. If `format.check` reports
   files you did not touch, leave them alone and list them in the report.

Exit checks:

- `git diff 63c40a9 -- docs/product` prints nothing.
- `git diff --stat 63c40a9 -- poc-v1` prints nothing.
- `make eol.check` passes.

---

## 7. Stop-and-ask conditions (complete list)

End your turn and ask the owner when any of these happens:

1. A change is needed in a file outside the package's allowed list.
2. Internal free or internal largest block after R3 is worse than at
   `63c40a9`.
3. The visible, paused, non-selected card behavior in R6 step 2.
4. The transition coordinator builds the incoming controller before the
   outgoing one releases its lease (R7 step 3).
5. A committed slot with missing identity has no specified outcome (R8 step
   1).
6. Legacy bridges still depend on `product_record` media ownership (R8 step
   2).
7. A new FreeRTOS task seems necessary, other than R5's audio task.
8. Directory open for resume can only run on the LVGL task (R3 step 2).
9. Metadata refresh latency becomes user-visible after R4.
10. Any experience, screen, or communication contract appears to conflict
    with what the code must do.
11. Any exit check fails and you cannot fix it inside the allowed files.

When you ask, give the evidence (file, function, numbers) and two or three
options. Do not pick one and continue.

---

## 8. Required report after each package

Use this template, filled in completely:

```text
Package: R<n>
Findings closed: F<..>
Files changed: <list>
Files intentionally not changed that looked relevant: <list + why>
Tests added: <names>
Exit checks:
  <command>  →  <result, pasted>
  ...
Memory notes: <every buffer > 1 KiB added/removed, size, region, lifetime>
Tasks: <any task created/removed, stack size, region, priority>
Open questions / stop-and-ask: <none | list>
```

Do not summarize exit-check output as "passed". Paste it.

---

## 9. Global self-checks (run before declaring the whole repair done)

```powershell
git diff 63c40a9 -- client/device/main/product_network.cpp
git diff --stat 63c40a9 -- poc-v1 docs/product
git grep -n "play_http" -- client/device/main
git grep -n "bsp_audio_codec_speaker_init" -- client/device/main
git grep -n "set_pcm_observer" -- client/device/main
git grep -nE "InkCache|visitInk|onInk|noteVisible" -- client/device/main
git grep -nE "fopen|fread|fwrite|fseek|std::remove|hashFile" -- client/device/main/product_playback_service.cpp client/device/main/product_inbox_view_controller.cpp client/device/main/product_runtime.cpp
git grep -nE "MALLOC_CAP_INTERNAL" -- client/device/main/product_runtime.cpp client/device/main/product_playback_worker.cpp
git grep -n "xTaskCreate" -- client/device/main
make test
make app:test
make app:build
make eol.check
```

Expected:

- The first three diffs and greps print nothing.
- `bsp_audio_codec_speaker_init`: exactly one call site.
- `xTaskCreate`: only tasks that existed at `63c40a9`, plus the R5 audio task.
- All `make` targets succeed.

---

## 10. Target verification (owner or agent with hardware)

Run after R3 and again after R7:

1. Flash `63c40a9` and the repaired build, saving the monitor log for each
   (`make app:monitor`).
2. Compare `product_mem::probe("ui_ready")` and the first probe after Wi-Fi
   association: internal free, internal largest block, DMA-capable largest
   block, PSRAM free.
3. Record boot-to-association time and every disconnect reason logged by
   `product_network`.
4. Exercise: login → Inbox → Play → Pause → resume → change selection →
   return → BOOT to Volume and back → mute → unmute → login → Play. Then
   RED → targets → record (audio plus multicolor sketch) → send → `sent` →
   recipient plays the message inside the card.
5. Record LVGL pool used, free, largest free, and maximum used; task stack
   high-water marks; and the actual region of every surface lease.

The acceptance lists in
[`device-ui-view-controller-third-slice-playback.md`](device-ui-view-controller-third-slice-playback.md)
and
[`device-ui-view-controller-fourth-slice-record-send.md`](device-ui-view-controller-fourth-slice-record-send.md)
remain the definition of done. This brief does not replace them.

---

## 11. Required reading

- [`device-ui-view-controller-memory-envelope.md`](device-ui-view-controller-memory-envelope.md)
- [`device-ui-view-controller-third-slice-playback.md`](device-ui-view-controller-third-slice-playback.md)
- [`device-ui-view-controller-fourth-slice-record-send.md`](device-ui-view-controller-fourth-slice-record-send.md)
- [`device-ui-remaining-slices.md`](device-ui-remaining-slices.md)
- [`device-ui-view-controller-second-slice-definition.md`](device-ui-view-controller-second-slice-definition.md)
- [`../../standards/device-presentation-invariants.md`](../../standards/device-presentation-invariants.md)
- [`../../standards/device-target-invariants.md`](../../standards/device-target-invariants.md)
- [`../../standards/client-application-coding-standards.md`](../../standards/client-application-coding-standards.md)
- `docs/product/experiences/message-carousel-and-playback.md`
- `docs/product/experiences/message-recording-and-sending.md`
- `docs/product/experiences/sketch-record-and-playback.md`
- `docs/product/communication-contracts/message-delivery.md`
