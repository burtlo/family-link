# Phase 2 — X02 + Opus size probe

Captured **2026-10-04** on commit `9788f0130fdc9826503f48f39eba032db6be6593` (same toolchain as [`phase1-summary.md`](phase1-summary.md)).

## Build variants

| Flash id | `FAMILY_DEMO` | Role |
|----------|---------------|------|
| `x02-opus-probe` | `x02_opus_size_probe` | Full X02 + `fl_opus_size_probe_force_link` + `opus` |
| `x02-opus-dep` | `x02_opus_dep_only` | Full X02 + `opus` dep only (negative control) |

Product **`x02` / `x02_product_shell`** unchanged (no Opus).

## Build results

| Demo | Build dir | Link | Partition check |
|------|-----------|------|-----------------|
| x02-opus-probe | `firmware/build/x02_opus_size_probe` | **OK** (`.bin` generated) | **FAIL** — overflow `0x1fb10` (129,808 B) |
| x02-opus-dep | `firmware/build/x02_opus_dep_only` | OK | OK (3% free, same as X02) |

Probe failure is `check_sizes.py` after link (`make build-firmware` exit 2); the measurement binary is still valid for size evidence.

## Measurement record (canonical table row)

App bytes = `wc -c` on `family_link_demo.bin`.  
App partition = **1,536,000** bytes (`SINGLE_APP_LARGE`, unchanged).  
Flash text / rodata from ESP-IDF **Memory Type Usage Summary** in `*-size.txt`.

| Build | App bytes | App partition bytes | Free bytes | Free % | Flash text | Flash rodata | DRAM | IRAM |
|---|---:|---:|---:|---:|---:|---:|---:|---:|
| X02 baseline (Phase 1) | 1,484,272 | 1,536,000 | 51,728 | 3.4% | 1,101,922 | 253,684 | 232,431 | 16,383 |
| **X02 + Opus size probe** | **1,665,808** | 1,536,000 | **−129,808** | **−8.5%** | **1,260,786** | **276,356** | 232,431 | 16,383 |
| X02 + Opus dep-only | 1,484,272 | 1,536,000 | 51,728 | 3.4% | 1,101,922 | 253,684 | 232,431 | 16,383 |

### Deltas

| Comparison | Δ app bytes |
|------------|------------:|
| Probe vs X02 baseline | **+181,536** (~177 KiB) |
| Probe vs dep-only control | **+181,536** |
| Dep-only vs X02 baseline | **0** |

**Headroom:** combined X02 + retained Opus is a **hard partition failure** on the current 1500 KiB factory slot (not merely marginal).

## Map audit

See [`phase2-map-audit.md`](phase2-map-audit.md). Probe retains `opus_encode` / `opus_decode` and `fl_opus_*`; dep-only strips them.

## Saved size reports

| Demo | size | size-components | size-files |
|------|------|-----------------|------------|
| probe | [`x02-opus-probe-size.txt`](x02-opus-probe-size.txt) | [`x02-opus-probe-size-components.txt`](x02-opus-probe-size-components.txt) | [`x02-opus-probe-size-files.txt`](x02-opus-probe-size-files.txt) |
| dep-only | [`x02-opus-dep-size.txt`](x02-opus-dep-size.txt) | [`x02-opus-dep-size-components.txt`](x02-opus-dep-size-components.txt) | [`x02-opus-dep-size-files.txt`](x02-opus-dep-size-files.txt) |

## Artifact index

```
firmware/demos/x02_opus_size_probe.c
firmware/demos/x02_opus_dep_only.c
firmware/common/fl_opus_size_probe.c
firmware/common/fl_opus_size_probe.h
firmware/main/CMakeLists.txt  (X02_V1_DEMOS, opus probe link opts)
scripts/flash.py               (x02-opus-probe, x02-opus-dep)
docs/evidence/x02-opus-partition/phase2-*.md
docs/evidence/x02-opus-partition/x02-opus-probe-*
docs/evidence/x02-opus-partition/x02-opus-dep-*
```
