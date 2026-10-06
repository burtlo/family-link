# H38 host source and mock foundation — 2026-10-06

This evidence records bounded host-only checks for the H38 filesystem experiment. It does not establish SD-card behavior or qualify firmware, hardware, DMA placement, or a storage backend.

## Results

- The production C disk guard compiled and passed 14 C fixture groups. Coverage includes zeroed/uninitialized state rejection, binding and phase order, byte and physical-LBA bounds, pointer overflow and DMA-buffer alias rejection, budgets and deadlines, exact accounting before dispatch, 8-sector driver-call limits, sticky failure after partial dispatch, and trim interception. The split-transfer fixtures verify all bytes of an 8+1 read and write.
- The pinned ESP-IDF 5.4.2 FatFs `ff.c` and shared H38 guard compiled together against a sparse host mock. `f_mkfs(FM_FAT32 | FM_SFD)` followed by the initial mount, under the aggregate FORMAT limits, wrote 2,062 sectors (1,055,744 bytes): FAT 1,025 sectors and 130,811 data clusters. The run made 263 FatFs write callbacks, 2 read callbacks, and 265 bounded driver calls; the largest observed callback and driver call was 8 sectors.
- The mock verified physical mapping within the fixed H38 volume, the FAT32 BPB, callback budget/range rejection, a synthetic 9-sector 8+1 split, and no retry after a partial backend failure. The single trim request was intercepted; the backend exposes no erase interface.

## Reproduction and limits

`disk-guard-checks.txt` and `format-footprint-checks.txt` contain the concise results. `process-results.json` records the actual exit code 0 for both host checks. The format report also records hashes of the pinned source and host fixtures so the checked inputs can be identified without publishing a private SDK installation path.

The host fixture uses an LFN-none FatFs configuration. Compare all effective H38 FatFs settings, including LFN options, against the actual device configuration before a firmware build. The host mock does not prove card writes, SDMMC timing, DMA-capable memory placement, reset or power behavior, filesystem behavior on the card, restore, or healthy boot. No SD-card operation or device build is included here. The storage backend remains unqualified; the reviewed source and controller contract, build, and hardware/restoration gates remain open.
