---
name: web-firmware-parity-check
description: Compare web twin and firmware timing/behavior constants before flash. Use after editing both demos/server/v1_product/web/box.js and firmware/demos/x02_product_shell.c.
disable-model-invocation: true
---

# Web/firmware parity check

## When to use

After editing both `demos/server/v1_product/web/box.js` and `firmware/demos/x02_product_shell.c`.

## Compare constants

| Constant | Web (`box.js`) | Firmware (`x02_product_shell.c`) | `docs/BOX-UI.md` |
|----------|----------------|-------------------------------------|------------------|
| Login/probe timeout | **MISSING** (no `CONNECT_PROBE_MS`) | `CONNECT_PROBE_MS` 2500 | 2.5 s |
| Hangout retry | **MISSING** (no `CONNECT_RETRY_MS`) | `CONNECT_RETRY_MS` 5000 | 5 s |
| Snap duration min | `SNAP_SCROLL_MS_MIN` 380 | `SNAP_SCROLL_MS` 380 | — |
| Snap duration max | `SNAP_SCROLL_MS_MAX` 720 | `SNAP_SCROLL_MS_MAX` 720 | — |

**Gap:** Web twin lacks explicit connect probe/retry constants; firmware defines them at lines ~124–125. Add or document web equivalents before claiming parity.

## Manual twin test

1. Start product server:

   ```bash
   python -m demos.server.v1_product.server
   ```

2. Open web twin: `http://localhost:8080/box/`

3. Run same PIN + carousel script as device checklist (see `device-test-after-flash` skill).

4. Note any behavioral divergence for spec update in `docs/BOX-UI.md` before flash.
