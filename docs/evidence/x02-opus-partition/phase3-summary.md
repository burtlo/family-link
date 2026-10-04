# Phase 3 — Attribute growth and reserve (summary)

**2026-10-04** · commit `9788f013` · builds in [phase1-summary.md](phase1-summary.md) / [phase2-summary.md](phase2-summary.md).

## Deliverables

| Document | Contents |
|----------|----------|
| [phase3-growth-analysis.md](phase3-growth-analysis.md) | X02 → probe component/object deltas, h31 upload estimate, demo-only exclusions, RAM/IRAM note |
| [phase3-planned-reserve.md](phase3-planned-reserve.md) | KiB ranges for missing long-message features + headroom policy math |

## Verdict line

**Headroom (probe on current 1,536,000 B factory app): _hard failure_** (overflow **129,808 B**). X02 alone was already **_marginal_** (**51,728 B** free &lt; **256 KiB** policy). **_Product-ready fit_** on this partition is **not achievable** with probe + **80–178 KiB** planned reserve + **256 KiB** policy margin (~**2.0–2.1 MiB** app slot needed).

## At-a-glance numbers

| Metric | Value |
|--------|------:|
| Probe growth vs X02 | **+181,536 B** (~177 KiB) |
| Attributed to Opus lib + wrapper + probe TU | **~99.1%** (175.0 + 0.6 + 0.1 KiB named) |
| Planned integration reserve (flash, not in probe) | **80–178 KiB** (mid **~125 KiB**) |
| Policy free-space threshold @ **current** 1.5 MiB slot | **262,144 B** |
| Policy margin @ **2.125 MiB** candidate slot | **334,234 B** — see [phase4-corrections.md](phase4-corrections.md) |

## Next phase

Phase 4 — compare partition strategies (corrected in [phase4-corrections.md](phase4-corrections.md)).
