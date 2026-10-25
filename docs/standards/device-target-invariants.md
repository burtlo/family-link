# Device target invariants (`client/device/`)

**Status:** Normative for product firmware under `client/device/`. Informative where
a lab adopts the same check. Does not override owner-approved experience specs
or OPEN contract items.

Behavioral MUSTs stay in
[client-application-coding-standards.md](client-application-coding-standards.md)
(§§1–2, 4, 7–8, 11–13). This profile states only the embedded facts those
sections cannot: heap capability, the two memory ledgers, current numeric
baselines, which functions are the presentation path, and which boot line
means what.

## Claim classes

| Class | Closes | Does not close |
|-------|--------|----------------|
| **tooling** | A host check failed a known-bad fixture | A desk journey |
| **compile** | `make app:build` and `device-build-report.json` | Flash, boot, or behavior |
| **flash** | The image was written | `product_ui_ready` |
| **boot-marker** | Serial contains `product_ui_ready` for that build id | PIN, record, or send behavior |
| **behavior** | An operator-authorized desk script plus serial | — |

A user-visible memory, stack, input, or lifecycle fix stays below **behavior**
until that artifact exists or the review packet records a waiver. `make test`
is tooling. `make app:build` is compile.

## Invariant catalog

| ID | Embedded rule | Proof |
|----|---------------|-------|
| MEM-CAP | A large buffer names its heap capability and lifetime. The panel draw buffer is internal DMA. `esp_get_free_heap_size` is not evidence. | `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC` logs the largest internal DMA block before display init and that block plus `lv_mem_monitor` after it. Log only. No numeric boot assert. |
| MEM-LEDGER | `CONFIG_LV_MEM_SIZE` is the object pool. Draw-buffer bytes are a separate ledger. Product baseline is 32 KiB. Some lab builds use 64 KiB. This program does not raise the pool. | `check_sdkconfig_defaults` |
| MEM-DELTA | Pool size and draw-buffer line count move only with a before/after DMA largest-block measurement. Runtime draw height is 16 lines in `product_display.c`. `CONFIG_BSP_LCD_DRAW_BUF_HEIGHT=40` is a different number. | Baseline test locks both numbers |
| STK-PATH | The LVGL timer path does not construct large frame objects. Floors until a manual `-fstack-usage` pass replaces them: main task 16 KiB, LVGL task 12 KiB. | Timer scan rejects a non-static `HttpResult`. Stack-usage aggregation is manual and not a CI gate. See [device-build-flash.md](device-build-flash.md). |
| PRES-PATH | §§1–2 and 13 already forbid blocking on the presentation path. The path is `on_login_timer` plus direct callees and hooks it assigns. Workers may block. | Timer scan; live firmware must scan clean. The SD card is mounted once in `app_main` before display init. Mounting it while the BOX-3 LCD runs left the panel white until power cycle (desk serial, 2026-10-24). |
| BOOT-MARK | `V2_BOOTSTRAP_READY` means the panel came up. `product_ui_ready` means shell and login attached. Product flash waits for both. | `product_flash_log_ok`. A fixture with only the bootstrap line fails. |
| CFG-FP | A desk-oriented build records build id and sdkconfig fingerprint. Product, lab-02, and lab-16 are different profiles. | `device-build-report.json` beside the bootstrap package. A missing kit does not fail the build. |
| ACC-NEG | A capture PASS rejects Guru Meditation, panic text, stack overflow, and a duration that contradicts the PCM payload (16-bit mono). | `verify_capture.py` and saved-log tests |
| INT-WIRE | A hardware producer with no caller under `client/device` is an open integration gap. Model tests do not close it. | Desk script `--strict-wiring` on a saved log. **Open:** nothing calls `product_record_push_pcm`. |
| LIFE-QUIESCE | Mutex, queue, and job storage are destroyed only after the worker acknowledges a terminal state. | Host model plus source scan. Lab 16’s stack upload job waits for `LAB16_UPLOAD_EVT_DONE`. **Open:** product login, inbox, and send HTTP stops delete the task and the queue in one function. This program does not rewrite those workers. |
| CLAIM | Every outcome uses one class from the table above. | Review packet fields below |

Operation envelopes, OPEN contracts, and one-failure-class changes stay as
links to §§4, 7, and 12. They are not restated here.

## Ownership

The LVGL login timer is the only presentation tick. It polls buttons first,
then runs login, inbox, volume, send, and record services, then presents one
screen. Phase order, visibility ownership, touch, and display power are
normative in
[device-presentation-invariants.md](device-presentation-invariants.md). HTTP
runs on separate workers. Record/send stage changes return through hooks
assigned on that same tick (`begin_wipe` reaches `ensure_media`).

## Desk checklist

Operator-authorized. Not a CI job. For login, inbox, record, and send, a saved
serial log needs `product_ui_ready`, and it must not contain Guru Meditation,
panic, or stack overflow. Record/send behavior additionally needs a non-zero
audio count and a send-worker line. Until INT-WIRE is repaired, run the checker
without `--strict-wiring`; that mode checks boot and forbidden text only.

```text
python3 scripts/device_guardrails/desk_record_send_check.py <saved-serial.txt>
python3 scripts/device_guardrails/desk_record_send_check.py --strict-wiring <saved-serial.txt>
```

## Review packet

[client-code-review-agent.md](client-code-review-agent.md) assignments that
touch `client/device` include:

- **Failure class** — one id from this catalog
- **Max claim class** — tooling, compile, flash, boot-marker, or behavior
- **Artifact path or waiver** — serial or build-report path, or an explicit waiver

A device-fixed claim whose only artifact is a build id is compile, not behavior.
Physical L3 is not a merge blocker in CI.

## Where the checks live

| Phase | Mechanism | IDs |
|-------|-----------|-----|
| 0 | `scripts/tests/test_device_guardrails.py`, `scripts/tests/test_lab16_capture.py` | PRES-PATH, STK-PATH, MEM-LEDGER, MEM-DELTA, ACC-NEG |
| 1 | `device-build-report.json`, `product_flash_log_ok` | CFG-FP, BOOT-MARK |
| 2 | Quiescence model and source scan; `CONFIG_FAMILY_DEVICE_MEM_DIAGNOSTIC` | LIFE-QUIESCE, MEM-CAP |
| 3 | `desk_record_send_check.py` on saved logs | INT-WIRE, ACC-NEG, BOOT-MARK |
