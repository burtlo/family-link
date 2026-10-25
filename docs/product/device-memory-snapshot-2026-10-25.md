# V2 device memory snapshot — 2026-10-25

Status: point-in-time evidence for the V2 product firmware. This is not a memory-budget approval.

## Latest diagnostic run — 2026-10-25

Build `80a3f355d76a40e88450691a3577cbb4` was flashed to the dynamically selected `lynn-kit` on COM5. Flash verification and both product boot markers passed. This run added the D M A total and free readings that were missing from the first capture. It supersedes the first-capture statements below about total D M A memory and post-Wi-Fi readings.

The allocator reported **243,547 B (237.84 KiB) total D M A-capable internal S R A M heap**. This is the managed D M A-capable portion of internal S R A M for this firmware run, not the BOX-3's full 512 KiB physical S R A M. The D M A readings do not include P S R A M; P S R A M is reported in its own columns below.

Between send-worker-ready and Wi-Fi-up, free D M A-capable internal S R A M fell by `99,231 - 28,723 = 70,508 B`; the largest contiguous available block of D M A-capable internal S R A M fell by `63,488 - 27,648 = 35,840 B`. Between Wi-Fi-up and login-worker-ready, free D M A-capable internal S R A M fell another `28,723 - 19,215 = 9,508 B`. These are changes across those startup intervals, not an exact per-component allocation ledger.

The device did **not** establish a confirmed connection to the requested server at `192.168.8.236:8777` in this run. The build's automatic workstation selection set its P1 URL to `192.168.8.199:8777`; no successful HTTP response was captured. The readings therefore show Wi-Fi and worker startup, not memory after a live-server response. Recording was not started.

This note separates physical memory capacity, free-heap readings, and known allocations. The LVGL allocator pool is intentionally outside this snapshot; display DMA buffers are included because they use DMA-capable internal S R A M.

## The short answer

- The BOX-3 has **512 KiB (524,288 bytes) of internal S R A M** and **16 MiB (16,777,216 bytes) of P S R A M**. Those are physical capacities, not free memory.
- This diagnostic build exposed **243,547 B (237.84 KiB) total D M A-capable internal S R A M heap**. At the latest captured phase, after Wi-Fi and login-worker startup, **19,215 B (18.8 KiB) of that D M A-capable internal S R A M heap were free**; its largest single free block was **18,432 B (18 KiB)**.
- Before Wi-Fi came up, free D M A-capable internal S R A M was **99,231 B (96.9 KiB)**. After Wi-Fi got an IP, it was **28,723 B (28.1 KiB)**. Thus the readings really do change during startup.
- The microphone hardware was initialized, but active recording was not tested. No successful server response was captured.

## Which memory is which

| Pool or term | Physical role | What this capture can say |
|---|---|---|
| Internal S R A M | 512 KiB on the ESP32-S3 chip. It holds internal heap and buffers that must be on-chip. | At `login_http_worker_ready`, the internal heap reported 251,687 B total and 26,975 B free. |
| P S R A M | 16 MiB of external working RAM. It is volatile RAM, not the S D card. | At `login_http_worker_ready`, the P S R A M heap reported 16,681,124 B total and 16,547,312 B free. |
| D M A-capable internal S R A M heap | A capability of internal S R A M blocks used for peripheral transfers; it is not a third RAM chip or a separate 512 KiB pool. The probe asks specifically for `MALLOC_CAP_DMA | MALLOC_CAP_INTERNAL`, so these readings are not P S R A M. | `dma_total=243,547 B`, `dma_free=19,215 B`, and `dma_big=18,432 B` at `login_http_worker_ready`. `dma_big` is the largest contiguous free block of D M A-capable internal S R A M, not the total free amount. |
| S D card / flash | Nonvolatile storage. The S D card is removable; flash stores firmware and settings. | The card mounted. Neither its capacity nor its mounted filesystem space is a RAM reading. |

`KiB` means 1,024 bytes. For example, `63,488 ÷ 1,024 = 62 KiB`.

## Boot-phase readings from the successful diagnostic flash

The table shows the logged startup phases, from before storage mount through Wi-Fi and login-worker startup. Values are bytes. `dma_total` (243,547 B) and `int_total` (251,687 B) are two overlapping views of internal S R A M heap; `psram_total` (16,681,124 B) is the separate external P S R A M heap.

| Boot phase | Time | D M A-capable internal S R A M free (`dma_free`) | Largest contiguous free D M A-capable internal S R A M block (`dma_big`) | Internal S R A M heap free (`int_free`) | External P S R A M heap free (`psram_free`) |
|---|---:|---:|---:|---:|---:|
| `storage_mount_before` | 19 ms | 140,999 | 63,488 | 148,759 | 16,678,572 |
| `storage_mount_after` | 63 ms | 139,983 | 63,488 | 147,743 | 16,649,320 |
| `mic_init_before` | 65 ms | 139,983 | 63,488 | 147,743 | 16,649,320 |
| `mic_init_after` | 93 ms | 132,927 | 63,488 | 140,687 | 16,646,112 |
| `ui_ready` | 464 ms | 104,523 | 63,488 | 112,283 | 16,645,532 |
| `send_http_worker_ready` | 469 ms | 99,231 | 63,488 | 106,991 | 16,629,144 |
| `wifi_got_ip` | 4,294 ms | 28,723 | 27,648 | 36,483 | 16,563,700 |
| `login_http_worker_ready` | 4,304 ms | 19,215 | 18,432 | 26,975 | 16,547,312 |

### What the subtraction does and does not mean

Microphone initialization bracket:

```text
internal S R A M heap free before - internal S R A M heap free after
= 147,743 B - 140,687 B
= 7,056 B less free internal S R A M heap

P S R A M free before - P S R A M free after
= 16,649,320 B - 16,646,112 B
= 3,208 B less P S R A M free
```

Those are changes across the whole BSP audio/codec initialization call, not an exact allocation ledger for the microphone alone. The `dma_big` reading stayed at 63,488 B across this bracket.

Between `mic_init_after` and `ui_ready`, free internal S R A M heap fell by `140,687 - 112,283 = 28,404 B`. The product display's known draw buffer accounts for 10,240 B of configured storage; the other `28,404 - 10,240 = 18,164 B` is an unassigned net change across display-driver, UI-module, and other startup work. The logs do not split that remainder by component.

For the first capture's `ui_ready` phase, the chip's 512 KiB spec could not be used as the denominator for `int_free`:

```text
512 KiB physical internal S R A M = 524,288 B
first-capture `ui_ready` free internal heap = 112,283 B
524,288 B - 112,283 B = 412,005 B
```

**412,005 B is not a valid “bytes used by the app” figure.** The probe reports free bytes in one allocatable heap capability; it does not report the total heap region or classify the rest of physical S R A M into app use, system use, and non-heap/reserved regions. That split remains unknown.

The latest run now reports allocator-managed totals. For example, at `login_http_worker_ready`:

```text
internal heap used       = 251,687 B - 26,975 B = 224,712 B
P S R A M heap used      = 16,681,124 B - 16,547,312 B = 133,812 B
D M A-capable heap used  = 243,547 B - 19,215 B = 224,332 B
internal outside heap    = 524,288 B - 251,687 B = 272,601 B
```

The D M A-capable internal S R A M heap is a subset of internal S R A M heap, so its numbers must not be added to the internal-S-R-A-M-heap numbers. `internal outside heap` is a physical-capacity remainder, not “firmware used”; it can include code/data regions and memory not managed as this heap.

Likewise, `147,743 - 63,488 = 84,255 B` is not “non-D M A memory.” One value is all free internal 8-bit heap; the other is the largest single D M A-capable block. A largest block cannot be subtracted from a total to recover the total D M A pool.

## Memory-related settings in the captured build

| Setting | Captured value | Meaning |
|---|---:|---|
| External P S R A M | Enabled; octal, 80 MHz | Enables the 16 MiB P S R A M heap. This is still volatile RAM. |
| `CONFIG_SPIRAM_TRY_ALLOCATE_WIFI_LWIP` | `y` | Allows Wi-Fi / LWIP allocations to try P S R A M; it does not guarantee every driver allocation lands there. |
| `CONFIG_SPIRAM_MALLOC_ALWAYSINTERNAL` | 16,384 B (16 KiB) | Default-allocation placement preference for small allocations. Explicit memory-capability requests, such as the worker stacks below, specify their own region. |
| `CONFIG_SPIRAM_MALLOC_RESERVE_INTERNAL` | 65,536 B (64 KiB) | Keeps internal heap available for requests that require internal memory while general allocations can use P S R A M. This is an allocator policy reserve, not 64 KiB owned by one feature or necessarily already allocated. |
| External task stacks | Allowed | The product's send, login, and microphone workers explicitly request P S R A M stacks. The network task uses ordinary `xTaskCreate`, so its actual stack region is not established by its call site. |
| Product display draw buffer | 16 rows, single buffer, D M A on, P S R A M off | `320 × 16 × 2 = 10,240 B` internal D M A buffer. This is set in product code; the BSP's general `CONFIG_BSP_LCD_DRAW_BUF_HEIGHT=40` is not the active product buffer height. |

## What was loaded, and what is known about its memory

| Feature | State in this capture | Memory evidence |
|---|---|---|
| S D storage | `bsp_sdcard_mount()` returned `ESP_OK`; log says `mounted=1`. It mounts before display startup. | The latest run sampled around mount: free internal S R A M heap and free D M A-capable internal S R A M each fell by 1,016 B; P S R A M free fell by 29,252 B. This is the net change across the mount call, not a per-allocation ledger. |
| Microphone hardware / I2S | Microphone codec initialization succeeded (`mic_init_after detail=1`). BSP code creates and enables I2S transmit and receive channels. | Free internal S R A M heap fell by 7,056 B and P S R A M free fell by 3,208 B across the initialization bracket. BSP defaults describe six DMA descriptors × 240 frames × 2 bytes per mono frame = 2,880 B of payload per direction, or about **5,760 B for both directions**, plus driver/descriptor overhead. This is configuration-derived, not a measured allocation total. |
| Active microphone capture | **Not active** in this boot-only capture. The stream opens and its task is created only after recording is armed. | Source configures a 6,144 B task stack and a 2,048 B PCM chunk in P S R A M when capture starts. Those allocations are not part of these idle-boot readings. |
| Display transfer buffer | Display initialized and UI became ready. | The active product code uses 16 rows, one buffer, and requests D M A memory: `320 pixels × 16 rows × 2 bytes RGB565 = 10,240 B = 10 KiB`. This is an allocated buffer, **not** 10 KiB free. The product-specific 16-row setting overrides the BSP default used elsewhere. |
| Wi-Fi | The latest serial capture logged `wifi_got_ip` at 4.294 s. | From `send_http_worker_ready` to `wifi_got_ip`, free D M A-capable internal S R A M fell by 70,508 B, its largest contiguous free block fell by 35,840 B, free internal S R A M heap fell by 70,508 B, and free P S R A M heap fell by 65,444 B. These are phase deltas, not an exact Wi-Fi-driver-only allocation count. |
| Login HTTP / roster attempt | The login HTTP worker reached its ready checkpoint at 4.304 s. | From `wifi_got_ip` to worker-ready, free D M A-capable internal S R A M and free internal S R A M heap each fell by 9,508 B; free P S R A M heap fell by 16,388 B. The worker requests a 16,384 B P S R A M stack, but the whole delta is not an exact allocation ledger. A successful server response was not verified. |
| Send HTTP worker | Serial reported `send HTTP worker ready` just after the `ui_ready` memory sample. | Worker requests a 16,384 B P S R A M stack, plus queues and HTTP state. The `ui_ready` number is just before that ready log, so it is not a post-worker memory sample. |
| Shell, inbox, send, volume, buttons, login UI | Their ready/armed logs were present by `ui_ready`. | Their startup effects are included in the `ui_ready` heap reading as a combined result; the probe does not assign bytes to each module. Inbox media and record/playback data were not loaded in this signed-out boot. |

## The two different “10 KiB” statements

There are two separate things that can be described as 10 KiB:

1. **A known 10 KiB display-buffer allocation:** the active product config allocates 10,240 B for its 16-row RGB565 D M A draw buffer.
2. **A documentation estimate of about 10 KiB internal heap free during recording:** this is a remaining-free-memory statement for a recording workload. The boot capture here did not record, so it neither measures nor verifies that estimate.

The first is an allocation. The second is a stated runtime remainder. Neither is the total S R A M capacity, P S R A M capacity, or a statement about S D-card space.

## Why the old H32 numbers are different

The archived H32 storage-only POC emitted three distinct fields:

- `free=` meant free space in its **on-chip flash outbox partition**;
- `heap=` was explicitly `heap_caps_get_free_size(MALLOC_CAP_INTERNAL)` — free internal heap;
- `psram=` was free P S R A M heap.

One H32 boot sample showed `heap=360,903 B` (352.44 KiB) and `psram=16,774,664 B`; after its flash-storage mount it showed `heap=359,483 B` (351.06 KiB) and `psram=16,631,128 B`. The H32 `heap_largest=262,144 B` was the largest internal heap block, not a D M A-qualified measurement. The formatted outbox's `free=14,323,712 B` was **storage space**, not RAM.

H32 was a separate storage qualification image, not the V2 product app; it did not include the product display, Wi-Fi/login flow, or microphone initialization. Its roughly 351 KiB free-heap figure therefore cannot be compared as though it were V2's remaining memory. The H32 run was also marked failed/unqualified for its storage cadence, so these values are useful for understanding the log fields, not as a product pass claim.

## What is still unanswered after the latest run

- **Memory during actual recording:** the run did not open the microphone stream or record.
- **Memory during an HTTP exchange with the intended server:** the build targeted `192.168.8.199:8777`, not `192.168.8.236:8777`; no successful server response was captured.
- **Per-module heap accounting:** the logs show combined free heap, not ownership of every allocation.
- **The exact source of the Wi-Fi-stage change:** the phase readings measure net changes across startup intervals, not per-allocation ownership.

## Capture method and provenance

- Hardware: dynamically selected `lynn-kit` on COM5; ESP32-S3-BOX-3, 16 MiB flash / 16 MiB P S R A M, 512 KiB internal S R A M.
- Successfully flashed diagnostic build: `dab1822bb0c241508f5e56cef8b180ce`, from source revision `755a8aac0145abf1d43d692953c4ebe6d36c449d` on `main`.
- The diagnostic was enabled for that run with `FAMILY_DEVICE_MEM_DIAGNOSTIC=1`; it did not change the default configuration.
- Boot memory records came from `logs/device/b7800f93413648c49c8a5b31009d44e6/lynn-kit.log`. The `logs/` directory is ignored by Git, so the key sanitized readings are copied into this tracked document. The live Wi-Fi monitor output was observed during the same capture session; the SSID is redacted here.
- The monitor filter included `product_network`, `product_mic`, `product_record`, `product_mem`, `product_send_http`, and `product_login`; it omitted `product_login_http`.
- No server was started for this RAM check. No HTTP response or authentication result is claimed.

During this documentation pass, additional memory checkpoints were added in source for storage mount, Wi-Fi-up, login/send HTTP-worker readiness, and microphone stream open. The diagnostic logs allocator-managed totals and free bytes for internal S R A M, P S R A M, and internal D M A memory, plus the largest contiguous internal D M A block. The first diagnostic build `60645702a4174b30b69bf65fe4c260bf` compiled but its flash attempt found COM5 busy; it was not the image measured below.

That first flash attempt was later completed successfully. Build `80a3f355d76a40e88450691a3577cbb4` was flashed to `lynn-kit` on COM5 and passed product boot-marker verification. The flash boot log is `logs/device/90185d13cafe450ab3a27ad96bae3014/lynn-kit.log`; the follow-up filtered serial capture is `logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt`. Clean `V2_MEM` samples extracted from that capture are in `logs/device/90185d13cafe450ab3a27ad96bae3014/memory-samples.txt`. The follow-up was captured after a monitor-triggered reboot, includes samples through `login_http_worker_ready`, and does not include a recording or successful server exchange.

The existing capture can be repeated after the serial port is free:

```powershell
$env:FAMILY_DEVICE_MEM_DIAGNOSTIC = '1'
make app
make app:monitor FILTER='product_network:I product_mic:I product_record:I product_mem:I product_send_http:I product_login:I product_login_http:I'
```

## Source locations

- [Memory probe](../../client/device/main/product_mem_probe.c)
- [Microphone capture](../../client/device/main/product_mic_capture.cpp)
- [Wi-Fi startup](../../client/device/main/product_network.c)
- [Product display buffer](../../client/device/main/product_display.c)
- [Storage mount and record path](../../client/device/main/product_record_bridge.cpp)
- [Login HTTP lifecycle](../../client/device/main/product_login_bridge.cpp)
- [Send HTTP worker](../../client/device/main/product_send_http_worker.cpp)
- [Product device README and its recording-memory note](../../client/device/README.md)
