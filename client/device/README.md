# Product device firmware (`client/device/`)

ESP-IDF application for the ESP32-S3-BOX-3 desk kit: LVGL shell, login, inbox,
record/send, and volume. Shared logic lives under `client/shared/` and is wired
through `main/product_*` bridges and HTTP workers.

## Build and flash

See [device-build-flash.md](../../docs/standards/device-build-flash.md).

```text
make app:build
make app KIT=<nickname>    # build + flash with deployment Wi-Fi/P1 overlay
```

Boot markers (distinct claims):

- `V2_BOOTSTRAP_READY build=<id>` — early boot after display init
- `product_ui_ready` — login shell attached on the LVGL timer

## Standards and guardrails

- Embedded invariants and claim classes:
  [device-target-invariants.md](../../docs/standards/device-target-invariants.md)
- Behavioral MUSTs:
  [client-application-coding-standards.md](../../docs/standards/client-application-coding-standards.md)

Mechanical checks (host):

```text
make test
```

Implementation lives under `scripts/device_guardrails/` (presentation timer path,
sdkconfig baseline, serial negatives, desk scripts against **saved** logs only).

`make test` is a tooling claim. It does not close record/send behavior.
The live timer still reaches `bsp_sdcard_mount` through `ensure_media`
(PRES-PATH), and nothing in this tree calls `product_record_push_pcm`
(INT-WIRE). Those stay open for the send repair.

Desk flash and live serial capture are **operator-authorized**; CI does not
require a connected kit. `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC` logs DMA
largest-block samples around display init. It does not assert a threshold.
