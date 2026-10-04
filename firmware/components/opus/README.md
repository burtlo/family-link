# Opus component (BOX-3 demos)

| Field | Value |
|-------|-------|
| **Registry package** | [`78/esp-opus`](https://components.espressif.com/components/78/esp-opus) **^1.0.5** |
| **Upstream** | [Xiph libopus](https://gitlab.xiph.org/xiph/opus) (bundled by the registry component) |
| **License** | BSD-style (see upstream `COPYING`) |
| **ESP-IDF** | 5.x (matches repo `idf-install` target) |
| **App wrapper** | `firmware/common/fl_opus.c` — 16 kHz mono, 20 ms frames |

This directory is a thin local component that depends on managed **`78__esp-opus`** (registry `78/esp-opus`). It compiles `firmware/common/fl_opus.c`. Demos link `opus` only when needed (`h30_opus_local`, `h31_opus_chunks`).

**Container (phase 1 decision):** device stores **length-prefixed raw Opus packets** for local soak tests. **Server-side Ogg Opus mux/finalize** is recommended for product seekable files (phase 3).
