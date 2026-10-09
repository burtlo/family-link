# Phase 0 — x02 LVGL slimming baseline

Captured **2026-10-08** on commit `6d1f8a8f11326af4b9076d5d535828338c84942a` ([`git-rev.txt`](git-rev.txt)). Toolchain: [`toolchain.txt`](toolchain.txt).

## Build

```bash
cd firmware
source ~/esp/esp-idf/export.sh   # or your IDF export
idf.py -D FAMILY_DEMO=x02_product_shell -B build/x02_product_shell fullclean build
```

Equivalent to `make build-firmware` / `scripts/flash.py --demo x02 --build-only` (same `-B`).

Partition check (build log): `family_link_demo.bin` **0x16a5f0** bytes; factory app **0x177000**; **0xca10** free (**3%**). Warning: nearly full.

## Measurement record

App partition = **1,536,000** bytes (`SINGLE_APP_LARGE` / 1500 KiB factory slot).

| Metric | Value | Notes |
|--------|------:|-------|
| App bytes (`wc -c` on `.bin`) | **1,484,272** | Hex `0x16a5f0` |
| Free bytes | **51,728** | Hex `0xca10`; **3.4%** free |
| Flash `.text` | **1,101,922** | `idf.py size` |
| Flash `.rodata` | **253,684** | fonts live here |
| DRAM used | **232,431** | DIRAM total |
| IRAM used | **16,383** / 16,384 | **99.99%** — unchanged constraint |
| Total image (ELF summary) | **1,484,160** | `.bin` may differ by padding |

## LVGL archive (`liblvgl__lvgl.a`)

Parsed from `python3 -m esp_idf_size --archives …/family_link_demo.map`:

| Column | Bytes |
|--------|------:|
| Flash total (`.text` + `.rodata` + appdesc) | **312,310** |
| DRAM (`.data` + `.bss`) | 66,152 |

## Top archives (flash total)

| Archive | Flash bytes |
|---------|------------:|
| liblvgl__lvgl.a | 312,310 |
| libesp_app_format.a | 115,252 |
| libnet80211.a | 107,351 |
| libmain.a | 102,628 |
| liblwip.a | 99,931 |

## vs 2026-10-04 x02-opus Phase 1

| Field | 2026-10-04 (`9788f013`) | Phase 0 (this run) |
|-------|------------------------:|-------------------:|
| App bytes | 1,484,272 | **1,484,272** (identical) |
| Free bytes | 51,728 | **51,728** |
| Flash `.text` | 1,101,922 | **1,101,922** |
| Flash `.rodata` | 253,684 | **253,684** |
| IRAM | 16,383 | **16,383** |

Same ESP-IDF **v5.4.2** class toolchain; byte-identical app image on this commit vs the historical snapshot.

## Artifacts

| File | Purpose |
|------|---------|
| [`x02-size.txt`](x02-size.txt) | `idf.py size` |
| [`x02-size-components.txt`](x02-size-components.txt) | `idf.py size-components` |
| [`x02-size-files.txt`](x02-size-files.txt) | `idf.py size-files` |
| [`sdkconfig-lvgl-source.txt`](sdkconfig-lvgl-source.txt) | Montserrat ladder from `sdkconfig.defaults` |

Linker map (not committed): `firmware/build/x02_product_shell/family_link_demo.map`.
