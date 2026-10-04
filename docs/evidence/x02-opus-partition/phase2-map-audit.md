# Phase 2 — Linker map audit (X02 + Opus probe vs dep-only)

Maps from clean builds on commit `9788f0130fdc9826503f48f39eba032db6be6593` (2026-10-04).

| Variant | Map path |
|---------|----------|
| **x02_opus_size_probe** | `firmware/build/x02_opus_size_probe/family_link_demo.map` |
| **x02_opus_dep_only** (negative control) | `firmware/build/x02_opus_dep_only/family_link_demo.map` |

## Probe (`x02_opus_size_probe`) — codec retained

`-Wl,-u,fl_opus_size_probe_force_link` pulls in `fl_opus_size_probe.c`, which calls `fl_opus_init`, encode, decode (including PLC / `packet == NULL`), both profiles via `fl_opus_set_profile`, and `fl_opus_deinit`.

Representative **flash-linked** symbols (address `0x42…`):

| Symbol | Address | Object |
|--------|---------|--------|
| `fl_opus_size_probe_force_link` | `0x420163b4` | `libmain.a(fl_opus_size_probe.c.obj)` |
| `fl_opus_init` | `0x42016c18` | `libopus.a(fl_opus.c.obj)` |
| `fl_opus_encode_frame` | `0x42016d4c` | `libopus.a(fl_opus.c.obj)` |
| `fl_opus_decode_frame` | `0x42016d84` | `libopus.a(fl_opus.c.obj)` |
| `opus_encode` | `0x4206b548` | `lib78__esp-opus.a(opus_encoder.c.obj)` |
| `opus_decode` | `0x42068c90` | `lib78__esp-opus.a(opus_decoder.c.obj)` |

Archive cross-refs in the map also show `opus_encoder_create` / `opus_decoder_create` reachable from `fl_opus.c.obj` and substantial `lib78__esp-opus.a` object files linked into `.text`.

**App image:** `1,665,808` bytes (`0x196b10`) — **overflows** factory partition `0x177000` by `0x1fb10` (**129,808** bytes).

## Negative control (`x02_opus_dep_only`) — stripped

Same full X02 sources and `opus` in `PRIV_REQUIRES`, but **no** probe TU and **no** `-u,fl_opus_size_probe_force_link`.

| Check | Result |
|-------|--------|
| `grep fl_opus` in map | **0** matches |
| `opus_encode` / `opus_decode` in map | **not present** (no `0x42…` definitions) |
| `esp-opus` in map | **2** `LOAD lib78__esp-opus.a` lines only (archive listed at link; code GC’d) |

**App image:** `1,484,272` bytes (`0x16a5f0`) — **identical** to Phase 1 X02 baseline.

## Conclusion

- The **size probe** measures the real combined cost: full X02 + live Opus encoder, decoder, and `fl_opus` wrapper (~**+181,536** bytes vs X02 baseline).
- The **dep-only** build proves that adding `opus` to `PRIV_REQUIRES` alone does **not** change image size; without forced references, the linker strips the codec.
- Map evidence satisfies Phase 2 acceptance: encoder and decoder entry points are present in the probe ELF; they are absent in the negative control.
