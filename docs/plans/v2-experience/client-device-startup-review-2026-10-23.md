# Client device startup and reboot review

Date: 2026-10-23  
Mode: Review only; no implementation changes made  
Target: `client/device/` product firmware and relevant shared login/session code  
Branch / HEAD: `main` / `4fa0301a6c80e48a18d8a5a41ad8718cff8cbe54`  
Current package at review time: `a5d8f200aa2d40a48709fe32b776d034`

This report records the source and runtime state reviewed on 2026-10-23. The
checkout had extensive uncommitted and untracked work. The login bridge changed
during the review; conclusions below use its later state, SHA-256
`65618329e7efa83c583a03d448d971fbda9270e2dd3e62631bfbf3fc386f785f`. Other
startup provenance hashes:

| Source | SHA-256 |
|---|---|
| `client/device/main/app_main.c` | `7bbe633ce894f3ce49c561950edf3d19f273746f94ba838b5ffc68e7094ba2c0` |
| `client/device/main/product_display.c` | `e41d1280163330ec403a2393b95aa46b6a5cd412d76b3e8d702ff94ec2cdd805` |
| `client/device/main/product_login_bridge.cpp` | `65618329e7efa83c583a03d448d971fbda9270e2dd3e62631bfbf3fc386f785f` |
| `client/shared/login_session/fl_product_login/credential_session.cpp` | `544b18145e6cf87fa9aac8f1d95bf30cc75d60d96746cc6bb5dd9305d22f3cbf` |
| `client/shared/login_session/fl_product_login/session.cpp` | `efe871589f74bd053e63689ff5a9499fa0a5f4cf8249aabf44253d1f773d60a8` |

## Governing sources and authority

| Source | Status and application |
|---|---|
| `docs/standards/client-application-coding-standards.md` | Normative engineering rules for bounded presentation work, single-writer session state, asynchronous operations, resource limits, and verification levels. |
| `docs/product/experiences/endpoint-login.md` | Interview draft; records owner-decided login behavior but claims no overall approval or implementation authorization. Governs roster, PIN, mute, and session behavior within those decisions. |
| `docs/product/communication-contracts/endpoint-health-and-login.md` | Draft contract with accepted wire decisions; governs health, roster, and login request behavior. |
| `docs/product/device-ui/screens/SCR-SIGN-IN-ROSTER.md`, `SCR-PIN-ENTRY.md`, `SCR-MESSAGE-INBOX.md` | Draft compositions; no visual approval claimed. Useful for composition requirements, not evidence of implementation readiness. |
| `docs/plans/v2-experience/implementation.md` and `orchestration-progress.md` | Planning and package evidence. Host L1/L2 results do not establish composed device behavior; the progress report records device-composed L2 as waived. |

## Boot and runtime evidence

### Earlier build: confirmed LCD allocation reboot loop

`logs/device/77872faf38cd463ba5622036af951f2f/box-a.log:83-98` records LVGL
draw-buffer allocation failure, followed by a BSP assertion in
`bsp_display_start_with_config` and `RTC_SW_CPU_RST`. This confirms a reboot loop
for that captured build. It does not establish the same failure in the current
source or package.

The current `product_display_start()` config uses one 25-line DMA-capable PSRAM
buffer and a 12 KiB LVGL task stack. The current device capture subsequently
initialized its display successfully.

### Current `a5d8` package: startup succeeds; later SPI error observed

The serial capture at
`/private/tmp/family-link-a5d8-2026-10-23.log:89-101` shows build `a5d8` reaching
`product_ui_ready`. At lines 149-153 it joins Wi-Fi, receives a valid two-user
roster, and creates roster cards. The log begins with the device already at
about 65 seconds uptime. Opening the serial port caused
`USB_UART_CHIP_RESET` at lines 1-3; this is a host-initiated USB reset, not
evidence of a spontaneous firmware reboot.

At lines 172-174, entering the record stage is followed by an LCD SPI transmit
queue error. This capture ends before a watchdog verdict. A different earlier
capture, `logs/device/22852ffd31024063b902ca0258acafcf/box-a.log:85-104`, records
an SPI color-queue error followed by repeated task watchdog alerts on `taskLVGL`.
The source/build provenance for that earlier capture is unavailable, so the two
events are a strong diagnostic lead but do not prove the current record path
caused a reboot.

`scripts/firmware.py:172-189` stops capture as soon as it sees
`V2_BOOTSTRAP_READY`. That marker occurs before the network task's two-second
delay and does not establish stable post-network or post-roster operation.

## Ownership and wait map

```text
app_main
  ├─ display, NVS, shell, login attach (LVGL lock held)
  └─ starts network task
       └─ Wi-Fi events → scheduled handoff flags

LVGL 50 ms login timer
  ├─ advances/drains the login session
  ├─ consumes HTTP results and updates models/UI
  ├─ polls hardware buttons
  └─ can attach the record scene and allocate its canvas

HTTP worker
  └─ performs blocking HTTP and posts result queue entries
```

Network HTTP runs in a worker, but session clock advancement, result handling,
roster rendering, hardware action dispatch, and record scene construction run
from the LVGL presentation path. The record entry work and the observed SPI
error need dedicated timing and target diagnosis.

## Findings

### F001 — Session clock can hang after authenticated mute

**Priority:** P1 · **Classification:** Confirmed defect in shared session model  
**Location:** `client/shared/login_session/fl_product_login/credential_session.cpp:107-126,230-261`; `client/shared/login_session/fl_product_login/session.cpp:789-845`

Mute arms a server invalidate at the current time. `credential_next_event_ms()`
keeps returning that already-due deadline, while `on_credential_clock()` does
not consume or move it. Advancing the clock past the deadline can therefore
repeat the same clock event indefinitely. The device invokes `advance_to()` from
its 50 ms LVGL timer, so a reachable authenticated mute can starve presentation
and trigger the watchdog.

A temporary bounded model harness established a signed-in session, muted it,
then called `advance_to(now + 1)`; it timed out after three seconds. The existing
mute/invalidate model scenario advances exactly to the due time and misses the
past-deadline case. This does not explain the initial boot reset, and the
current device UI cannot yet reach authentication through touch.

**Requirement:** Client coding standard §§2, 4, and 7; accepted mute/invalidate
behavior in `COM-016`.  
**Smallest fix class:** Ensure an immediate invalidate deadline is dispatched
once or excluded from clock scheduling after dispatch. Preserve asynchronous,
non-blocking mute behavior.  
**Verification:** Test advancing beyond the deadline, queue-full handling, and
exactly-once invalidate dispatch. Device confirmation is still required before
claiming the watchdog path fixed.

### F002 — Same-count roster reorder can select the wrong user

**Priority:** P1 · **Classification:** Confirmed source defect  
**Location:** `client/device/main/product_login_bridge.cpp:85-120,161-218`

Card creation caches only the roster count and focused user ID. Each click
stores a positional index, then resolves that index against the current model.
If a same-count roster response reorders users while focus stays unchanged, the
rendered cards retain the old order while clicks resolve in the new order. A
tap can select someone other than the visible card.

**Requirement:** Client coding standard §§4-5 and the owner-decided roster
identity/order behavior in `EXP-LOGIN`.  
**Smallest fix class:** Associate each rendered card and callback with immutable
user ID; refresh the view on roster content changes while preserving focused
identity.  
**Verification:** Same-count reorder followed by tap, same-count name/background
update, focused-user removal, then target touch confirmation.

### F003 — PIN entry is not wired on device

**Priority:** P1 · **Classification:** Confirmed integration gap  
**Location:** `client/device/main/product_login_bridge.cpp:85-99,284-355`

Roster-card taps call `select_pin_user()`, which moves the session to PIN entry,
but no device code calls `enter_pin_digit()` or renders the keypad. The render
path continues to show the roster whenever the stage is roster and PIN phase is
not cooldown. A person cannot complete sign-in from the device.

**Requirement:** Owner-decided PIN journey in `EXP-LOGIN`; composition in
`SCR-PIN-ENTRY` remains draft and visually unapproved.  
**Smallest fix class:** Implement the PIN screen and device input path from the
session snapshot, including cancellation, timeout, and failure states.  
**Verification:** Script the roster-to-PIN path, then confirm it on target.

### F004 — Inbox remains placeholder text without a device fetch path

**Priority:** P1 · **Classification:** Confirmed integration gap  
**Location:** `client/device/main/product_inbox_bridge.cpp:20-59,64-104`

The inbox bridge creates and updates labels, but has no inbox HTTP worker or
carousel card presentation. Host model/journey tests do not establish a usable
device inbox.

**Requirement:** `EXP-MESSAGE-CAROUSEL` and `SCR-MESSAGE-INBOX` draft composition.  
**Smallest fix class:** Connect the device inbox operation to its worker and
render model snapshots into the specified carousel and status states.  
**Verification:** Script valid, empty, stale, connecting, and refresh scenarios;
then verify on target.

### F005 — Health request reports fixed boot and mute state

**Priority:** P2 · **Classification:** Confirmed contract mismatch  
**Location:** `client/device/main/product_login_http_worker.cpp:275-281,440-445`

The health payload always says `reason=periodic` and `muted=false`; its boot ID
is sourced from session identity rather than a unique boot identifier. The
request can therefore misrepresent boot reason and mute state. This is not a
demonstrated reboot cause.

**Requirement:** `COM-001`, `COM-009`, and `COM-010` in
`endpoint-health-and-login.md`.  
**Verification:** Assert request bodies for boot, connectivity return, periodic,
and muted cases, then inspect a target/server capture.

### R001 — Record entry performs scene allocation and drawing on LVGL timer

**Priority:** P1 diagnostic concern · **Classification:** Confirmed unbounded
presentation work; causal reboot link unproven  
**Location:** `client/device/main/product_hardware_buttons.c:91-94`,
`product_stage_router.cpp:20-31`, and `product_record_bridge.cpp:54-110`;
timer dispatch in `product_login_bridge.cpp:357-376`

The red-button event reaches record attachment from the LVGL timer path.
Attachment allocates a 240×240 RGB565 canvas (115,200 bytes), creates a widget
tree, fills the canvas, and builds swatches. The current target capture records
an SPI queue error immediately after record-stage entry. An older unrelated
provenance capture has the same class of SPI error followed by LVGL watchdogs.
Do not label this the current reboot cause until a longer trace captures the
first failure and any resulting reset.

**Requirement:** Client coding standard §§2 and 8; target time and resource
budgets remain unmeasured.  
**Verification:** Capture the entire record transition and subsequent watchdog
window, retain the SPI return/error code, and measure LVGL task time, stack, heap
high-water, and largest free block on target.

## Execution plan

1. **Diagnose the current SPI/watchdog path.** Obtain a sustained serial trace
   through record-stage entry and the full watchdog window. Include build ID,
   reset reason, SPI error code, and decoded panic/backtrace. The flash marker
   check alone is insufficient.
2. **Fix F001 and verify it independently.** This has a model-level reproduction
   already; exercise past-deadline, queue, and terminal-event boundaries before
   a target mute scenario.
3. **Fix F002 before enabling PIN submission.** Stable identity must be attached
   to visible cards before a card tap can authorize a PIN attempt.
4. **Complete F003, then F004.** PIN entry is required to reach signed-in inbox
   behavior. Verify host/scripted scenarios before target interaction.
5. **Correct F005.** This can proceed independently of the UI work after its
   request state inputs are defined.

The model clock, health payload, and SPI diagnosis do not depend on one another.
The UI order above is sequential because PIN depends on correct roster identity,
and inbox depends on sign-in.

## Evidence achieved and remaining

| Claim | Evidence level | Evidence / limit |
|---|---|---|
| Earlier LCD draw-buffer startup reboot | L3 | Saved serial log shows allocation failure, BSP assertion, and software reset for that earlier build. |
| Current `a5d8` display, Wi-Fi, and roster startup | L3, short observation | 20-second target capture; device had already run about 65 seconds before host serial-open reset. Does not establish long-duration stability. |
| Current record-stage SPI failure | L3, incomplete outcome | Target log records SPI queue error; capture ends before watchdog/reset outcome. |
| Invalidate clock hang | L1 | Bounded model harness timed out when advancing past the immediate due time. No device reproduction because PIN flow is not wired. |
| Roster identity, PIN, inbox, and health findings | L0/source review | Reachable source conditions/contracts identified; target interaction evidence remains open. |
| Current roster-card memory and render-time budget | Unmeasured | Eight-user high-water and render timing were not measured. Allocation failure fallback can also mark a partial card tree as complete. |

No integrated build or new test suite was run for this review. No implementation
files were edited. The shared working tree was already dirty and remained so.
