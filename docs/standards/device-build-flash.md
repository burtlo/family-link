# Product device build and flash

The v2 product firmware under `client/device/` is a full ESP32-S3 (ESP-BOX-3) image:
display, LVGL shell, login, inbox, record/send, and volume. Kit nicknames select
physical positions; endpoint credentials come from deployment overlays via
`make app KIT=<nickname>`.

A **legacy bootstrap path** still exists inside the same binary: early boot logs
`V2_BOOTSTRAP_READY build=<build-id>` after display init. When the login shell
attaches successfully, the firmware also logs `product_ui_ready`. Desk acceptance
must treat those markers as **distinct** claims (see
[device-target-invariants.md](device-target-invariants.md)).

## Daily commands

```text
make build              # Bootstrap-style package (no deployment overlay)
make app:build          # Product build; optional KIT= for Wi-Fi/P1 sdkconfig overlay
make app:flash          # Flash current package (one connected kit or KIT=)
make app                # app:build then app:flash
make devices
make flash.mazi         # Named kit targets (deployment alias)
```

`make build` and `make app:build` require the configured ESP-IDF version but no
USB kit for a plain build. Each successful build records an immutable package
under `artifacts/device/bootstrap/<build-id>/` and updates `current.json`.
The merged `firmware.bin` contains bootloader, partition table, and application;
the package retains component images, ELF, effective sdkconfig, partition layout,
IDF flash metadata, size report, SHA-256 hashes, and (after `make app:build`)
`device-build-report.json`.

### Toolchain version pin

`idf.path` (`IDF_PATH`) must point to a Git checkout for which
`git describe --tags --exact-match` returns the configured `idf.version`
(currently `v5.4.2`). See prior sections in this doc for fork/shallow-clone rules.

`make flash` / `make app:flash` require exactly one connected registered kit
unless `KIT=` is set. Flashing verifies package hashes; it does not rebuild.
A missing kit fails flash. It does not fail `make app:build`.
Product serial capture after flash requires both
`V2_BOOTSTRAP_READY build=<build-id>` and `product_ui_ready`.
The bootstrap line alone is not product UI boot
(see `scripts/device_guardrails/` and `client/device/README.md`).

## Explicit USB binding

```text
make devices
make device.bind KIT=arlo PORT=/dev/cu.usbmodem…
```

Binding updates only the selected kit's USB serial in local deployment YAML.

Settings use `config/host.defaults.ini` (`firmware.source`, `firmware.build`,
`firmware.artifacts`, `firmware.logs`, `firmware.boot_seconds`, `idf.*`).
`make install` remains the explicit setup step.

## Verification ladder (product)

| Level | Meaning for product |
|-------|---------------------|
| L0 | `make app:build` succeeds; sdkconfig baseline tests pass |
| L1 | Host model/scripted tests for touched shared libraries |
| L2 | Host fault-injection / serial checker on saved logs |
| L3 | Operator-authorized desk run: flash, serial artifact, journey script |

`make test` covers mechanical guardrails (presentation path, sdkconfig lock,
serial negatives). It does **not** replace L3 desk evidence for user-visible fixes.
Behavioral MUSTs remain in
[client-application-coding-standards.md](client-application-coding-standards.md)
(§§1–2, 4, 7–8, 11–13).

### Stack usage (manual)

Developers may run an IDF build with `-fstack-usage` and inspect `.su` files for
`app_main`, the LVGL task, and the HTTP workers. That pass informs STK-PATH
review. CI does not gate merges on stack-usage aggregation.
