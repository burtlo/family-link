# Product device firmware (`client/device/`)

ESP-IDF application for the ESP32-S3-BOX-3 desk kit: LVGL shell, login, inbox,
record/send, and volume. Shared logic lives under `client/shared/` and is wired
through `main/product_*` bridges and HTTP workers.

## Build and flash

See [device-build-flash.md](../../docs/standards/device-build-flash.md).

```text
make app:build
make app KIT=<nickname>    # build + flash with deployment Wi-Fi/P1 overlay
make app:monitor FILTER='product_mute:I product_login:I'   # serial log (KIT= PORT=)
```

`app:monitor` needs a prior **`make app:build`** (or `make app`) so
`FAMILY_BUILD_ID` and `build/device/sdkconfig` exist; it reuses the current
package’s build id from `artifacts/device/bootstrap/current.json`.

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
make app:test    # client/device + shared libraries (config/app-test-suite.yaml)
make test        # full repository suite
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

## Mute / black-screen diagnostics

Enable verbose ticks in menuconfig: **Family Link product login → Verbose mute
latch and display diagnostics**, or set `CONFIG_FAMILY_DEVICE_MUTE_TRACE=y` in
`sdkconfig` before `make app:build`.

Serial filter: `make app:monitor FILTER='product_mute:I product_login:I'` (or plain
`idf.py monitor` from `client/device`). Tags: `product_mute`, `product_login`.

| Log prefix | When |
|------------|------|
| `V2_MUTE_EDGE` | Every physical latch transition (`latch=0` unmuted, `latch=1` muted). Always on. |
| `V2_MUTE_DISPLAY` | Backlight blank (`blank`) or wake (`wake`). |
| `V2_MUTE_EDGE reason=dark_tick` | Every 2 s while `dark=1` (only with `CONFIG_FAMILY_DEVICE_MUTE_TRACE`). |

**Capture procedure:** Repro once (inbox → mute → mute again). Save serial from
the first `V2_MUTE_EDGE latch=1` through 10 s after the second mute toggle.
Interpretation:

- **`latch` stuck at 1** after you unmated: GPIO/button disagree with the switch
  (`gpio` vs `btn` columns).
- **`latch=0` but `dark=1` and no `V2_MUTE_DISPLAY wake`**: software never left
  blanked backlight.
- **Roster visible after unmute but touch dead, serial shows `gpio: GPIO[3]` during
  wake**: firmware called `bsp_display_exit_sleep()` without a matching
  `enter_sleep()`; use brightness-only wake (BOX-3 touch INT is GPIO 3).
- **`latch=0`, `wake` logged, `stage=muted`**: session unmute not running.
- **`muted_blank=1` forever with `latch=0`**: timer thinks UI should stay off
  despite open latch (stale `stage` or `dark` flag).

Host check on a saved log (no device): grep `V2_MUTE_EDGE` and
`V2_MUTE_DISPLAY` in the capture file.
