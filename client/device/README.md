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
`FIRMWARE_BUILD_ID` and `build/device/sdkconfig` exist; it reuses the current
package’s build id from `artifacts/device/bootstrap/current.json`.

Boot (serial tag `app_main`; normative order **BOOT-ORDER** in
[device-target-invariants.md](../../docs/standards/device-target-invariants.md)):

- `app_main.c` — kit bring-up through NVS, then `product_app_run()`
- `build=<id>` — first line in `app_main`
- Subsystem lines (`product_record` SD mount, `product_mic`, `product_display`, …)
- `boot_step=*` / `boot_backlight …` — display and initial backlight policy
- `product_ui_ready build=<id>` — shell and login attached; flash guardrail waits for this
- Wi-Fi starts after the ready line (`product_network`)

## Standards and guardrails

- Embedded invariants and claim classes:
  [device-target-invariants.md](../../docs/standards/device-target-invariants.md)
- Behavioral MUSTs:
  [client-application-coding-standards.md](../../docs/standards/client-application-coding-standards.md)
- Presentation integration (one tick, one visibility owner, touch, display
  power, desk matrix). Read before changing any screen:
  [device-presentation-invariants.md](../../docs/standards/device-presentation-invariants.md)

The module map in one line: `on_login_timer` in `product_login_bridge.cpp` is
the only LVGL timer. It polls buttons, advances login, services inbox, volume,
send, and record, then calls `product_stage_present()` in
`product_stage_router.cpp`, which shows exactly one module.

Mechanical checks (host):

```text
make app:test    # client/device + shared libraries (config/app-test-suite.yaml)
make test        # full repository suite
```

Implementation lives under `scripts/device_guardrails/` (presentation timer path,
sdkconfig baseline, serial negatives, desk scripts against **saved** logs only).

`make test` is a tooling claim. It does not close record/send behavior.
The SD card is mounted once in `app_main` before the display starts
(`product_record_mount_storage`); powering the card slot while the LCD runs
leaves the BOX-3 panel white until power cycle. The microphone codec is set up
in `app_main` before the display for the same reason (`product_mic_init`): its
I2S DMA buffers take about 7 KiB of internal RAM, and no block that large is
free once a recording starts. `product_mic_capture.cpp` reads 16 kHz mono PCM on
its own task from wipe start until finish and appends it under the record
store lock; storage only syncs the card when a take is committed.

Ring frame rate (open, accepted for now): the ring-wipe and finish collapse
show about 5–6 frames over 600 ms on the desk (2026-10-24). Frames advance only
on the 50 ms presentation tick, and large-radius frames take 100–200 ms to
redraw nearly the full screen through the 16-line DMA draw buffer. Measure
render vs flush time per frame before changing buffers; internal RAM has about
10 KiB free during recording.

Naming gap (open): the record path is built on shared modules still named for
the lab that produced them (`lab10_capture`, `lab10_render`, `lab10_ui`, and
`LAB10_*` constants). They are v2 product code. A later pass renames them and
every caller and test to `fl_sketch_*` / `FL_SKETCH_*`; new code should not add
more lab-named identifiers.

Desk flash and live serial capture are **operator-authorized**; CI does not
require a connected kit. `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC` logs DMA
largest-block samples around display init. It does not assert a threshold.

## Mute / black-screen diagnostics

Enable verbose ticks in menuconfig: **Family Link product login → Verbose mute
latch and display diagnostics**, or set `CONFIG_FAMILY_DEVICE_MUTE_TRACE=y` in
`sdkconfig` before `make app:build`.

Serial filter: `make app:monitor FILTER='product_mute:I product_login:I product_stage:I'` (or plain
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
- **`latch=0`, `dark=0`, but no `product_stage: screen=` line after unmute**:
  the compositor did not change screens; check the session stage, not LVGL.

Host check on a saved log (no device): grep `V2_MUTE_EDGE` and
`V2_MUTE_DISPLAY` in the capture file.
