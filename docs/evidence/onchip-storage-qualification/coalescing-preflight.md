# H32 write-coalescing preflight: no hardware candidate

**Date:** 2026-10-05. **Result:** the proposed 16 KiB cadence-only `fwrite`
coalescing change stopped at its read-only feasibility gate. No firmware,
partition, device, or raw evidence was changed by this preflight.

The analyzed no-yield H32 build used ESP-IDF v5.4.2 at commit
`f5c3654a1c2d2a01f7f67def7a0dc48e691f63c0`, effective SDK configuration
SHA-256 `97f6c180de19702bf67b0ee89682899cf40eaa49945ea3809d6b21cbc83547b8`.
The generated configuration enabled 4,096-byte FatFs and wear-leveling
sectors. H32 requested a 4,096-byte allocation unit in
`firmware/demos/h32_onchip_storage.c`.

Source trace in the pinned local ESP-IDF:

1. `components/fatfs/vfs/vfs_fat_spiflash.c` passes the requested allocation
   unit through `esp_vfs_fat_get_allocation_unit_size` to `f_mkfs`.
2. `components/fatfs/src/ff.c` divides the requested 4,096 bytes by the
   4,096-byte sector size, creating one sector per cluster. The observed first
   format succeeded under this setting.
3. The same `ff.c` `f_write` limits each direct `disk_write` call to the
   remaining sectors in its current cluster.
4. `components/fatfs/diskio/diskio_wl.c` `ff_wl_write` erases and then writes
   each requested range.

Consequently, a sector-aligned 16 KiB application submission still becomes
four 4 KiB data requests with this geometry. Larger `fwrite` calls might
reduce stdio or VFS overhead, but that overhead is unmeasured and this change
does not reduce the dominant sector erase/write count. Wear-leveling metadata
can add operations; this trace does not quantify every physical operation.
The mounted boot sector was not independently read, so the one-sector cluster
is inferred from the successful format and source rather than measured from
media bytes.

An independent reviewer reached the same no-go conclusion. The plan's gate
requires a defensible coalescing mechanism before hardware mutation, so the
candidate was stopped. The prior no-yield PCM result remains a failure with
44 scheduled misses and 33 service misses. H32 remains unqualified. The next
planned experiment is qualification of the attached medium, including its
actual device-visible interface and durable write cadence.
