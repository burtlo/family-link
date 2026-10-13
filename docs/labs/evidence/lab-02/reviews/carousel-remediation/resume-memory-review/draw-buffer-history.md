# Draw-buffer startup history — focused retrospective

Scope: the LAB-02 startup allocation incident and the historical v1 comparison. This is historical analysis, not a review of the concurrent retry repair. No source edits, builds, host tests, hardware operations, or flash were performed. Frozen build artifacts were copied before the fixer rebuilt them; hashes are in `snapshot-hashes.json`, primary excerpts in `primary-extracts.json`.

## Conclusion and owner decision

**Confirmed:** v1's saved configuration used a **100-line draw buffer**, 16-bit color, and double buffering disabled. It called `bsp_display_start()`. The LAB setting of **50 lines was selected and tested without trying another value**. It is a successful startup mitigation, not an optimized height. Preserve the owner's instruction to revisit the buffer if it matters to performance or resource safety; do not describe 50 as a measured optimum or automatically tune it.

**Confirmed:** the earlier successful LAB-02 image exposed 23,440 more bytes in its first startup RAM heap region than the later failing image. **Strongly supported:** firmware static-storage changes materially reduced the available internal RAM. **Unknown:** the exact largest DMA-capable block at the allocation site, the complete symbol-by-symbol difference between the two actual booted images, and how much fragmentation versus preceding startup allocations determined the failure threshold.

## What changed between earlier success and failure?

| First boot evidence | First RAM region | Display outcome |
|---|---|---|
| `logs/device/labs/lab-02/20261012-w8-manual-01/serial.txt`, lines 87, 93–96, 105, 119 | start `3FCC6FB0`, length `0x22760` = 141,152 bytes | LVGL starts; `LAB02_READY`; ELF prefix `335597630` |
| `logs/device/labs/lab-02/20261013-fb8c80cd3c/serial.txt`, lines 87, 93–96, 105, 113 | start `3FCCCB40`, length `0x1CBD0` = 117,712 bytes | `buf1` allocation fails; BSP assertion/reboot; ELF prefix `acd642803` |
| `logs/device/labs/lab-02/20261013-1f6025a663/serial.txt`, lines 87, 93–96, 105, 119 | same `3FCCCB40`, `0x1CBD0` | 50-line configuration reaches `LAB02_READY`; ELF prefix `47c246de8` |

The other three printed startup regions are unchanged. The first region's start moves upward by `0x5B90` = **23,440 bytes**, while its length falls by that amount. This is evidence of reduced startup memory before display construction, not a later HTTP workload observation. These initial regions are not measurements of the largest DMA block immediately before allocation.

The frozen successful phaseb ELF SHA-256 is `47c246de85f957ff8eb75a75241a7fdedab0dda770ab24e28354974b8257b0d3`, matching the successful log prefix. Its map has `.dram0.data` at `0x3FC9DE00`, size `0x5388`; `.dram0.bss` at `0x3FCA3188`, size `0x299B8`, ending **exactly at `0x3FCCCB40`**. Thus the new heap boundary corresponds to the image's static DRAM boundary. Lowering draw-buffer height leaves that boundary unchanged and reduces a later dynamic allocation.

Historical source snapshots establish the relevant storage transition: `c1a-implementation/before-session.c` used static `s_post_buf` and `s_pump_ev`; the C1b-before session snapshot already has nine reserved terminal slots; the C3a after-session snapshot adds a separate immutable `s_pending_revision`. In the frozen target map, `s_terminals` occupies `0x5A00` = **23,040 bytes**, and `s_pending_revision` occupies `0x9F0` = **2,544 bytes**. These are persistent internal allocations, even before a worker runs. Replacing previous storage must also be counted; do not simply add these two numbers and call them the total regression.

**Attribution limit:** retained older non-phaseb ELFs have different hashes and a different static boundary; they are not the W8 booted ELF. Their maps also retain a local-provider model later removed. They cannot supply an exact comparison for the 23,440-byte W8-to-failure delta. The actual W8 and failed ELF/map pair is needed to allocate that entire delta among symbols, alignment, and section changes. The terminal/pending-storage mechanism is strongly supported, but sole-cause and exact-unit attribution are not established.

## Why did v1 and earlier LAB-02 work?

V1's height is historical fact, not a transferable RAM budget. V1's saved dependency lock used BOX-3 BSP **3.2.0**; LAB used **3.2.1**, with different BSP component hashes. Both locks specify LVGL port **2.9.0** and LVGL **9.6.0~1**, with matching component hashes. The v1 managed BSP implementation is unavailable in the saved archive, so identical v1 allocation flags are not independently proven. Both saved configurations enable PSRAM with a 16,384-byte always-internal threshold and 32,768-byte internal reserve; enabled PSRAM alone does not establish where the display buffer was allocated.

For LAB, the managed-source allocation analysis already recorded in `report.md` establishes: width 320, RGB565 at 2 bytes/pixel, one partial-render draw buffer, DMA flag enabled, PSRAM flag disabled. Height 100 asks for **64,000 contiguous DMA-capable bytes**; height 50 asks for **32,000**, saving **32,000 bytes**. The panel remains 320×240. The earlier image's successful startup and larger initial region explain why an unchanged 100-line request could succeed earlier and fail after persistent-memory growth. Display starts before the HTTP worker's Wi-Fi connection; Wi-Fi runtime allocation is not established as the initiating cause.

The proximate failure is the unsatisfied buffer allocation. The contributing resource-envelope change is the smaller startup RAM region. The systemic gap is progressing from compile/host acceptance to further implementation without representative target startup and resource-budget evidence. C2/C3a/C3b/C3c verification explicitly retained missing L3 evidence; host LVGL/adapters and link success do not execute this target DMA allocator or reproduce its memory capabilities, so those checks can pass while board startup fails.

## Tradeoff and remaining measurements

Smaller partial buffers reduce the largest contiguous allocation and retained memory. For a full 240-line repaint, 100 lines permits three height-sized portions, while 50 permits five; real dirty regions may differ. Smaller chunks can increase flush/setup/scheduling overhead; the BSP's SPI maximum-transfer setting also follows configured height. This does not prove worse frame latency: total pixel payload for an equal repaint is unchanged, and fill/transfer overlap, scene dirtiness, scheduling, and driver behavior must be measured. No p95 or optimal-height claim follows from startup success.

For C5, capture total/free/minimum and **largest allocatable DMA/internal block immediately before and after display allocation**, requested bytes/alignment/capabilities, and relevant earlier startup allocation deltas. Retain the exact ELF/map/config hashes with each boot. Measure stack/heap high-water and fragmentation after representative carousel operation and network/retry pressure. If buffer importance warrants comparisons, use a small explicitly chosen height set and identical scene/input workload; capture flush count/duration, input responsiveness, frame timing, and startup margins. Do not infer performance from height alone.

Use the existing client coding standard §8 resource-budget gate: declare persistent/transient resource limits and measure them on representative target configurations, including the largest block where fragmentation matters. Portable invariant: a bounded queue or mailbox can still consume enough persistent memory to invalidate a peripheral's largest-allocation requirement; resource verification must include the whole linked image and the real startup sequence. No new standards or autotuning implementation is proposed.
