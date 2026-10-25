# V2 UI Memory and Screen Lifecycle Review

Date: 2026-10-25  
Reviewed source: main at a79b4fe0da605794a1caa74329c7515f1cdc3d6b  
Review type: read-only source and document review  
Status: investigation and proposal; not an approved implementation plan

## What this review answers

This report records:

1. What the 32 KiB LVGL setting, the Record canvas, and DMA refer to.
2. Whether one parent view owner and a shared, leased pixel buffer are feasible.
3. What each current screen transition builds, hides, deletes, or keeps alive.
4. What a four-stage view lifecycle would require.

The code review was read-only. No build, test, flash, device measurement, commit, or push was performed. The device measurements quoted below come from the existing [2026-10-25 device memory snapshot](../../product/device-memory-snapshot-2026-10-25.md); that capture did not enter Record or exercise a successful send.

## Short answer

- **32 KiB is the one global LVGL memory pool configured for this product.** Labels, buttons, cards, and other LVGL-managed allocations draw from this shared pool. It is not a 32 KiB allocation for each screen.
- **The Record canvas is separate:** 240 × 240 pixels × 2 bytes per RGB565 pixel = 115,200 bytes = **112.5 KiB**. The first Record attach requests internal 8-bit memory, then falls back to PSRAM. The request does not ask for DMA memory. The pointer is kept after the Record widgets are released, so a successful first attach reserves that pixel buffer for the rest of the boot.
- **The LCD DMA draw buffer is a third allocation:** 320 × 16 pixels × 2 bytes = 10,240 bytes = **10 KiB** of internal DMA-capable memory. This is the screen flush buffer, not the drawing canvas.
- The V2 code reads touch coordinates through LVGL and writes ink pixels with the CPU into the Record canvas. It does not allocate the touch path or canvas with a DMA capability. The microphone codec uses its own internal DMA buffers. SD driver/bounce-buffer use is inside the storage stack and is not assigned a product-owned byte count here.
- A central view owner is feasible and the stage router is already the single visibility owner. It can grow into a lifecycle coordinator that keeps a minimal split-pane shell, builds only the active scene, and deletes outgoing scene widgets after transitions.
- A shared **pixel-buffer lease** is also feasible for compatible pixel data if only one consumer owns it at a time and each view relinquishes all pointers before the next lease. It cannot become extra LVGL object memory. Reserving 112.5 KiB from internal memory at boot is not yet shown safe by the saved readings; the existing path deliberately has a PSRAM fallback.

## Memory inventory

“Memory” here means several separate allocators and hardware regions. The byte totals below should not be added together: internal and DMA capability queries can describe overlapping physical blocks.

| Item | Current size or reading | What it is for | What the source establishes |
|---|---:|---|---|
| Physical internal SRAM | 512 KiB = 524,288 B | On-chip RAM shared by firmware, stacks, drivers, UI, and eligible DMA allocations | Hardware capacity; not the amount free to the app |
| Physical PSRAM | 16 MiB | External RAM used by eligible allocations | Separate from internal SRAM and DMA-capable internal heap |
| LVGL memory pool | 32,768 B = 32 KiB | LVGL objects and other LVGL-managed allocations | Product setting in client/device/sdkconfig.defaults. One shared pool, not per view |
| Internal allocation reserve | 65,536 B = 64 KiB policy | ESP-IDF allocator keeps internal heap available for requests that cannot use PSRAM | CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL; not a feature allocation and not a standalone RAM chip |
| LCD draw buffer | 10,240 B = 10 KiB | Sixteen RGB565 display rows used for LCD flush | client/device/main/product_display.c requests a DMA buffer and disables PSRAM for it |
| Record canvas | 115,200 B = 112.5 KiB | 240×240 RGB565 pixels that hold the active sketch | Internal 8-bit allocation attempted first; PSRAM 8-bit fallback; neither request includes DMA |
| One 120×120 sketch image | 28,800 B = 28.125 KiB | A card-sized RGB565 sketch image | Inbox may keep an image per sketched card |
| Selected playback frame and paint scratch | 57,600 B = 56.25 KiB combined | Two 120×120 frames used by inbox sketch handling | Separate from the Record canvas and the LVGL allocator |
| Microphone codec initialization delta | 7,056 B less internal free in the saved capture | Net change across the BSP microphone/codec initialization call | This is not a precise microphone-only allocation ledger; the codec driver owns its internal DMA allocations |

The inbox supports up to 64 messages. A 28,800-byte image for every one would be 1,843,200 B, about 1.76 MiB, before LVGL objects and other buffers. That is a theoretical upper bound from the per-card image size and message limit, not a measured current allocation. It makes the idea of keeping one active sketch frame plus smaller saved card images worth investigating. It does not prove that the present code allocates all 64 images at once.

### What the saved device readings say about an early internal canvas

The saved capture reported allocator readings for **internal SRAM** and **external PSRAM** separately. Every DMA reading below is for **DMA-capable internal SRAM**, not PSRAM:

| Startup phase | Internal SRAM heap free | DMA-capable internal SRAM heap free | Largest contiguous free DMA-capable internal SRAM block |
|---|---:|---:|---:|
| After microphone initialization | 140,687 B | 132,927 B | 63,488 B |
| UI ready | 112,283 B | 104,523 B | 63,488 B |
| Wi-Fi got IP | 36,483 B | 28,723 B | 27,648 B |
| Login HTTP worker ready | 26,975 B | 19,215 B | 18,432 B |

If the 115,200-byte canvas were allocated immediately after the saved mic-init sample, the simple aggregate subtraction would leave:

**140,687 B internal free − 115,200 B canvas = 25,487 B internal free**

That is only an arithmetic scenario, not proof that a 115,200-byte contiguous internal allocation would succeed. The saved report did not record the largest general internal block at that point, and it did not capture the canvas pointer's actual region. At the saved UI-ready sample, aggregate internal free was already 2,917 B smaller than the canvas:

**112,283 B − 115,200 B = −2,917 B**

At the saved Wi-Fi-connected sample it was smaller by 78,717 B. Therefore, if the Record attach happens with a similar heap state, its internal request cannot fit and the PSRAM fallback is expected. **The current capture did not reach Record, so the actual buffer region on the device is still unmeasured.**

The 32 KiB LVGL pool does not make the 112.5 KiB pixel buffer, and deleting LVGL objects returns their allocations to the LVGL pool; it does not hand that memory to the general heap as a 112.5 KiB canvas allocation. The 64 KiB internal reserve is an allocator policy, not a protected pool owned by the canvas, microphone, or display.

### DMA and the sketch input path

DMA describes memory/peripheral transfer capability, not an additional RAM chip. In this capture, `dma_total`, `dma_free`, and `dma_big` use `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`: they measure DMA-capable **internal SRAM heap**, never PSRAM. `int_total` and `int_free` use `MALLOC_CAP_INTERNAL | MALLOC_CAP_8BIT`, while `psram_total` and `psram_free` use `MALLOC_CAP_SPIRAM`. The displayed 243,547-byte DMA-capable internal SRAM total is not the whole 512 KiB physical SRAM and is not a separate pool that can be added to internal-SRAM-free readings. The DMA-capable values overlap the internal-SRAM heap values; PSRAM free is reported separately.

The names and allocations in the current V2 source establish these separate paths:

- LCD pixels are copied/flushed through the configured 10 KiB internal DMA draw buffer.
- The codec driver initializes microphone capture with internal DMA resources. The saved 7,056-byte difference brackets the whole BSP init call, not each driver buffer.
- LVGL touch events provide positions to the sketch handler. The sketch handler changes pixels in the Record canvas with CPU writes, then invalidates the canvas so the LCD redraw path can flush it.
- The Record canvas allocation does not request DMA. Do not label it “DMA memory” without measuring the returned pointer and the allocator capability effect.
- Product storage uses the storage/filesystem stack. Its internal DMA and bounce-buffer use is not enumerated as a product-owned allocation in the reviewed source.

To settle the canvas placement question in a future device capture, log the allocation result and explicitly classify the returned pointer as internal, external RAM, and DMA-capable. Record capability free/largest-block values immediately before and after. A change in free DMA bytes alone is not an ownership ledger for a non-DMA allocation.

## Parent view and buffer-lease proposal

### One parent view owner

The device already has one presentation owner: product_stage_present() is called from the single LVGL presentation tick and decides which feature modules are visible. The missing piece is lifecycle ownership. Today it mostly changes visibility; it does not consistently tell a screen when to build, enter, leave, and destroy its widget tree.

A parent controller can own:

- The active LVGL screen root and, while using split-pane scenes, the persistent upper/lower shell slots.
- Which scene is active and the order of its enter/leave hooks.
- The transition boundary at which outgoing widget trees may be destroyed.
- A reference to a separate pixel-buffer lease manager.

The persistent shell should contain only the structure the product wants always present, plus any common header labels that are explicitly reused. Login roster, PIN pad, inbox cards, recipients, volume slider, and sending feedback can be mounted into its slots only when needed. Record uses its full-screen layout; split-pane scene widgets can be removed for that stage and the shell restored on exit.

This fits the owner’s idea without turning the split pane into a mode-heavy widget. The active layout pattern already specifies a shell with upper and lower panel slots. Reuse the shell and common title/subtitle labels where they truly have the same role; keep PIN indicators, playback controls, roster cards, recipient cards, and volume controls as small scene-specific widgets.

### Four lifecycle points

An implementation could give each scene these hooks, all run on the existing LVGL owner task:

| Hook | Responsibility |
|---|---|
| viewWillDisappear | Stop accepting input for the outgoing scene; cancel its animations/timers safely; copy transient focus or selection into the session/model if it is not already there; ask the buffer manager to begin release |
| viewDidDisappear | After the transition no longer needs the old tree, delete its LVGL objects, invalidate every stored object pointer, and return any exclusive buffer lease |
| viewWillAppear | Acquire any required buffer lease and build only this scene’s widgets from current session/model state |
| viewDidAppear | Enable input and start only the scene-owned visual activity that should run while it is on screen |

For an instant transition, the outgoing widgets can be deleted before building the destination to minimize overlap. For the Record ring-wipe, keep whatever pixels/objects the animation needs until it finishes, then release the picker and acquire the Record lease. If a transition intentionally keeps both trees alive briefly, its peak memory must be measured and budgeted.

### What “loan the Record canvas” can mean

There are two different kinds of memory involved:

1. **LVGL objects** (buttons, labels, cards, styles, and object metadata) use the configured LVGL allocator. The 115.2 KiB canvas cannot be substituted for this allocator just because it is larger.
2. **Pixel storage** (the Record image or an image frame used by a view) can use a shared fixed arena if its consumers follow an exclusive lease.

A buffer manager could allocate one backing pixel arena and lend it to a compatible consumer. Before a lease changes owner, the old consumer must stop drawing/playing, finish any DMA or display operation referencing the memory, and either preserve its needed result in a smaller independent image or discard it. The next owner must not see a pointer into the former owner’s live image.

For example, a card may show a saved 120×120 sketch image while the one larger working frame is used only by the selected/active card. On unpause or restart, the selected card reacquires the working frame and reconstructs its editable/playback state from the saved representation. That would reduce simultaneous large frames, but the current implementation does not have that lease contract. The exact saved format, image ownership, and resume semantics remain to be decided against the sketch and carousel experience requirements.

Preallocating a backing arena at boot is technically possible, but it reserves memory from that moment. It does not leave the same bytes available to Wi-Fi or arbitrary heap allocations unless those components explicitly use the arena as compatible storage. The saved startup arithmetic makes an internal 112.5 KiB reservation risky; the current code’s PSRAM fallback is why first Record entry can still succeed when internal memory is low. The owner has also reported visible lag when interactive carousel/sketch components use PSRAM. Treat that as a product performance constraint: do not move interactive pen, playback, PIN, or carousel work to PSRAM without a measured interaction trial. First identify the actual current canvas region and measure the internal reservation against Wi-Fi, recording, and transitions.

### Likely architecture boundary

Keep three owners distinct even if the parent controller coordinates them:

- **Session/services:** auth, inbox messages and playheads, recipient choice, recording/send job, upload worker, volume setting, and storage stay alive as required by product behavior.
- **View host:** owns LVGL objects, screen hooks, input enablement, transitions, and pointer invalidation.
- **Pixel-buffer broker:** owns backing memory and exclusive leases for active canvas/playback/image operations.

The stage router can become the view host. The modules should expose separate “initialize service once” and “build/destroy this view” operations. Calling an existing attach function on every appearance is unsafe: several attach functions initialize or reset session state.

## LAB-16 and V2 test plan location

The detailed LAB-16 variant results, clean baseline, memory tables, and V2 communications scenarios have moved to the [LAB-16 and V2 device memory test plan](../../../client/labs/lab-16-attached-storage/README.md). This lifecycle review stays focused on V2 screens, UI allocations, and scene transitions.

## Current lifecycle and scene-by-scene review

### As-built structure

The stage router is already the one visibility writer and the product uses a single 50 ms LVGL timer. That is a useful foundation. The current differences from the requested lifecycle are:

- Login attach eagerly attaches/builds Inbox, Send, and Volume views along with Login.
- On every non-Record presentation after the split pane has been released, the rebuild function builds Login, Inbox, Send, Volume, and login lower-slot widgets regardless of the active screen.
- Presentation then hides all but the active module. Hidden trees still occupy LVGL pool memory and any independent card-image buffers they own.
- Some modules expose build/forget functions, but the APIs do not consistently distinguish service initialization from view lifetime.
- The Record path releases the split-pane slot trees before attaching the full-screen Record view. Returning from Record can rebuild all four split-pane modules.
- Current Record surface release deletes its LVGL tree but keeps the 115.2 KiB canvas pointer and Record session. The pointer is a boot-lifetime reservation after first allocation.

### Transition-by-transition findings

| Transition | Current behavior | Waste or lifecycle concern | Target behavior for a view-hook design |
|---|---|---|---|
| Boot → signed-out Login roster | Login attach also attaches/builds Inbox, Send, and Volume UI. Login roster is built for the signed-out scene. | Three inactive screen trees and related UI state are created before they can be used. | Create the persistent bare split shell and Login roster only. Initialize services/models once where required, without mounting their widgets. |
| Login roster → PIN | Roster cards are cleared and the PIN pad is created when needed. | This direction already deletes the roster card widgets, but the reverse transition retains the keypad hidden. | Keep shell/common header; delete roster widgets; build PIN widgets. Update the PRES-LEVEL2 invariant that currently says to hide, not delete, the PIN pad on return. |
| PIN → incorrect / cooldown | Error/cooldown is mostly a state/copy update within Login. | No need to retain a second full screen. Any transient overlay/timer must have a clear owner and end point. | Update text/state in the Login scene; dispose any temporary indicator when the state ends. |
| PIN → Login roster via BOOT | The Login layer is shown and the PIN pad remains hidden for reuse. | Hidden keypad LVGL objects stay allocated despite the product flow having no ordinary reason to revisit that PIN instance. | Delete PIN widgets after the return transition; rebuild roster from the current roster/session model. |
| PIN → Inbox after successful sign-in | Login presentation is hidden; Inbox presentation is shown. The module widgets were already built during Login attach. | Login and other inactive screen widgets remain resident. | Preserve auth/session and Inbox data; delete Login/PIN visual trees and build Inbox only. |
| Inbox → Volume via BOOT | Visibility changes; Volume title and slider were already built at boot. | Inbox visuals/card images and Volume widgets coexist while one view is hidden. | Preserve Inbox selection and playhead in the model; destroy Inbox view, build Volume view. |
| Volume → Inbox via BOOT | Visibility changes back to the already-built Inbox. | Volume widgets stay allocated after leaving. | Save volume state through its service/model; destroy Volume widgets and rebuild Inbox from its saved state. |
| Inbox → Recipients via RED | Send picker is shown; Inbox widgets are hidden. Inbox playback is paused by the send path. | The Inbox tree and its card image buffers remain allocated under the hidden state. | Preserve selected message/playhead/sketch in the Inbox model; pause playback as specified; destroy Inbox view and build Recipients. |
| Recipients → Inbox via BOOT | Picker is hidden; Inbox is made visible. | Picker cards/labels remain allocated until a later rebuild or broader split release. | Delete picker view and rebuild Inbox from its model. Preserve recipient state only if the product contract requires it on that exit. |
| Recipients → Record via RED | Stage router releases the split-pane trees and ensures Record. Canvas is allocated lazily on this first Record attach. | The view transition has no explicit lease transfer; the allocation region is not logged; Record attach failure is ignored by the stage router. | Complete the ring-wipe boundary, destroy picker widgets, acquire the exclusive Record pixel-buffer lease, build Record, and handle attach failure as a real stage outcome. |
| Record → Recipients on cancel or record failure | Stage/state changes and presentation rebuilds the split-pane modules; Record surface visibility/release is not consistently tied to the screen transition. | Rebuilding all modules costs LVGL pool space; a retained Record tree/buffer can outlive the active view. | Stop or discard capture according to the state outcome; destroy Record widgets after the reverse transition; return the buffer lease; build only Recipients and preserve selection as specified. |
| Record → Sending / sent receipt / Inbox | Send state and upload can continue beyond one screen paint; the stage router rebuilds the split pane on return. | View cleanup must not tear down the finished message or its upload worker. | Commit the finished take to the service/outbox, destroy the Record view at the transition boundary, return the canvas lease when no renderer references it, and mount only the current send or Inbox view. |
| Sending view → Inbox while upload continues | Product behavior allows the upload operation to outlive a screen. | Current “attached” flags and widget-release functions mix view and service lifetimes. Calling full Send release as view cleanup could stop worker processing. | Delete sending widgets while leaving the send service/event processing and upload worker alive; build Inbox. Resume/update send feedback only when that view is shown again if required. |
| Mute / sign-out | Feature-specific cleanup exists, but split modules and service state do not all share one explicit scene teardown contract. Finished send work may persist across sign-out per experience rules. | A blanket module release risks deleting durable work or stopping a permitted background upload; leaving hidden widgets consumes memory. | Blank display and destroy signed-in visual trees; preserve finished outbox/send state and service work required by the send contract. Do not retain raw LVGL pointers in session objects. |

### Important service-versus-view boundary

Keep these alive independently from a screen:

- Authentication/session identity and roster/inbox models.
- Inbox selection, playback position, and saved sketch state.
- Recipient selection and finished recording/send state.
- Storage, microphone/network workers, and any upload still authorized to run.
- Volume setting and its persistence model.

Destroy/rebuild these with the scene as appropriate:

- LVGL roots, labels, cards, buttons, sliders, temporary overlays, and scene-owned animations.
- Scene-specific buffers that are not shared or needed by a live background worker.
- Cached LVGL pointers to objects that have been deleted.

The current Send attach resets journey/stage state, so it must remain a one-time service initialization and must not be called each time the Send view appears. Likewise, do not use product_send_release_widgets as a generic screen destructor: current code ties that path to service/worker lifetime. Add a view-only destroy operation and keep service pumping independent from whether Send feedback is mounted.

Inbox card images and active sketch/playback frames deserve a separate allocation review. Current card synchronization can materialize images for loaded messages; the selected playback frame and scratch image are separate buffers. A one-active-frame design may reduce peak memory, but it needs a defined snapshot/resume flow and a measured internal-memory budget. Do not assume deleting the Inbox widget tree also deletes every service-owned sketch state.

## Findings and evidence classification

### Confirmed current gaps

1. **Inactive views are eagerly built and retained.** Login attach constructs Inbox, Send, and Volume UI. The stage router hides inactive modules. On split-pane rebuild, all module widgets are rebuilt. This conflicts with the requested “only the current scene is mounted” lifecycle.
2. **There is no shared will/did appearance lifecycle contract.** Central visibility ownership exists, but transition preparation, post-transition deletion, input shutdown, and buffer handoff are not consistently represented as module hooks.
3. **Record canvas region and DMA status are not recorded.** The allocator requests internal memory then PSRAM but does not request DMA or log which branch succeeded or whether the pointer is DMA-capable.
4. **Send service and Send view lifetimes are coupled.** Current attach resets send journey state, and full release affects service/worker state. A new lifecycle must separate building/destroying UI from keeping a finished send alive.
5. **The PIN lifecycle rule differs from the owner’s clarified direction.** The current PRES-LEVEL2 says PIN is hidden, not deleted, when returning to roster. The owner now explicitly accepts deleting it. Update the invariant as part of any implementation package.

### Supported risks

1. **Early internal canvas reservation may compete with Wi-Fi and UI startup.** The saved mic-init internal-free figure leaves 25,487 B after a hypothetical 115,200 B reservation, before later initialization. The later saved UI-ready internal-free total is already below the canvas size. Largest contiguous internal allocation was not captured, so allocation success cannot be inferred from total free.
2. **Using a single shared sketch frame needs strict exclusive ownership.** Any playback, card image, animation, or display operation that still references the arena must quiesce before Record acquires it. The code currently has no such lease protocol.
3. **Destroying widgets must not discard state or stop live workers.** Session/model data and in-flight work survive some screen exits by product requirements. Raw LVGL pointers must not survive tree deletion.

### Evidence/document reconciliation needed

- device-presentation-invariants.md PRES-LEVEL2 currently requires hiding the PIN pad on return to roster; revise it to match the accepted deletion direction before implementation.
- device-target-invariants.md and SCR-RECORD-SKETCH.md contain a statement that product_record_push_pcm has no caller, but the current microphone capture task calls product_record_push_pcm. Reconcile that stale invariant/spec note before using it as a wiring conclusion.
- No target measurement in this review establishes the exact current Record canvas region, active Record peak, or simultaneous product Send+Record peak.

## Suggested implementation sequence after this review

This is an ordering proposal, not an implementation authorization or approved resource budget.

1. Reconcile the PIN deletion invariant and the stale PCM-call documentation. Confirm the intended state to preserve on every exit in the Login, Inbox, Sketch, and Record/Send experience sources.
2. Define the view-host lifecycle contract and the separate view/service flags. Keep the single LVGL tick and sole presentation owner; specify what each scene snapshots and which service continues after view destruction.
3. Implement the low-risk split-pane transitions first: Login↔PIN, PIN→Inbox, Inbox↔Volume, and Inbox↔Recipients. Destroy outgoing scene widgets after their transition and rebuild from models.
4. Separate Send service initialization/event pumping from Send widget mounting. Prove that an upload continues while its receipt view is unmounted.
5. Add the pixel-buffer lease abstraction and Record lifecycle only after defining image snapshot/resume behavior. Instrument the selected buffer’s actual region, allocation success/failure, lease owner, and release point.
6. Measure view overlap and memory at scene boundaries and during active Record, SD writes, upload, and upload-plus-next-Record. Decide whether early allocation and the one-active-sketch-frame plan fit measured internal memory and latency.
7. Independently verify the view transitions and worker/state preservation on the physical device. A host build or object-pool arithmetic alone cannot qualify touch responsiveness, PSRAM latency, DMA placement, or peak concurrent usage.

## Review boundary and evidence

- Source/config reviewed on main at a79b4fe0da605794a1caa74329c7515f1cdc3d6b, before this report was created.
- Cross-checked against the client coding standards, device-target and presentation invariants, LVGL memory budget foundation, split-pane panel contract, relevant experience specifications, and product memory snapshot.
- A separate read-only client reviewer confirmed the main memory and lifecycle conclusions.
- No implementation files were edited. No build, test, flash, or new device memory capture was run.

## Main source references

- Product memory configuration: client/device/sdkconfig.defaults:8,18
- LCD DMA buffer: client/device/main/product_display.c:6-20
- Record canvas allocation, canvas setup, and surface release: client/device/main/product_record_bridge.cpp:396-415,439-440,492-508
- Memory probe: client/device/main/product_mem_probe.c:12-46
- Stage routing and presentation ownership: client/device/main/product_stage_router.cpp:16-32,53-72
- Eager attach and split-pane release/rebuild: client/device/main/product_login_bridge.cpp:965-996,999-1040
- Inbox sketch image and widget ownership: client/device/main/product_inbox_bridge.cpp:332-424,930-950; client/device/main/product_inbox_carousel.hpp
- Send view/service lifecycle: client/device/main/product_send_bridge.cpp:919-954,1164+
- Volume attach/build/visibility: client/device/main/product_volume_bridge.cpp:117-173,251-263
- Product memory evidence: docs/product/device-memory-snapshot-2026-10-25.md
- PIN lifecycle invariant: docs/standards/device-presentation-invariants.md
- Resource measurement and scene-lifecycle guidance: docs/product/device-ui/foundations/FOUNDATION-LVGL-BUDGET.md; docs/product/device-ui/patterns/PANEL-MODULE-CONTRACT.md
