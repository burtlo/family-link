# Orchestration progress — v2-experience

Status: **complete** (P1–P10 host L1/L2; device composed L2 waived)

| Package | Implement | Verify | Notes |
|---------|-----------|--------|-------|
| P1 | completed (+ hardening) | pass (independent) | 12 host tests; `p1-hardening-notes.md` |
| P2 | completed (+ hardening) | pass (independent) | CAT-21–CAT-37; `p2-hardening-notes.md` |
| P3 | completed | pass | `client/shared/stage_shell/`; `V2_PRODUCT_SHELL_READY` |
| P4 | completed | pass (host L1/L2) | `client/shared/login_session/`; P1 wire; device login mount |
| P5 | completed (+ hardening) | pass (independent) | 12+7 session tests; `p5-hardening-notes.md` |
| P6 | completed (+ repair) | pass (host L1/L2) | 11+2 inbox tests; P1 Range 206 |
| P7 | completed | pass (host L1/L2) | `client/shared/record_capture/`; device record mount |
| P8 | completed (+ repair) | pass | 12+2 send tests; LAB-11 green; `p8-hardening-notes.md` |
| P9 | completed (+ repair) | pass (host L1/L2) | 10 volume L1/L2; `p9-hardening-notes.md` |
| P10 | completed | pass (host L1/L2) | journey + manifest; 28-test regression; device L2 waived |

## Package P1

### Implementer (initial)

- `server/device/` + `scripts/tests/test_lab_p1_host.py`.

### Hardening + independent verifier

- See [`p1-hardening-notes.md`](p1-hardening-notes.md). **12** tests pass.

## Package P2

### Implementer

- **Screens:** `SCR-MESSAGE-INBOX`, `SCR-RECIPIENT-PICKER`, `SCR-SEND-FEEDBACK`, `SCR-VOLUME`
- **Panels:** message sender header, inbox carousel, recipient picker header/body, send feedback, volume slider
- **Catalog:** scenes **CAT-21**–**CAT-37** (37 scenes total, revision 5)
- **LAB-14:** `lab14_ui.c` renderers for new panel types; `generated/catalog.json` repacked
- **Tests:** `device_ui_catalog.py validate/pack`, `test_device_ui_catalog` (3), `make lab.lab14.build`

### Verifier (initial)

- **Verdict:** pass; gaps: MISSING.md stale, CAT-23 header mismatch.

### Hardening (operator-authorized)

- **Notes:** [`p2-hardening-notes.md`](p2-hardening-notes.md)
- **Changes:** Refreshed [`screens/MISSING.md`](../../product/device-ui/screens/MISSING.md); CAT-23 → `PANEL-HEADER-MESSAGE-SENDER`; `SCR-MESSAGE-INBOX.md` empty-state note.

### Independent verifier (post-hardening)

- **Verdict:** pass · **hardening_validated:** true · **gaps:** none

## Package P3

### Implementer

- **`client/shared/stage_shell/`** — `fl_shell_model` + `product_shell` (cover-down, session writer)
- **`client/device`** — BSP display + shell mount; boot marker **`V2_PRODUCT_SHELL_READY`**
- **Tests:** `test_lab06_model` (15), `test_product_shell_model` (6)

### Verifier

- **Verdict:** pass
- **Gaps:** no kit serial for shell ready line; `fl_shell_model` forked from LAB-06 (sync risk); full `make lab.lab06.scripted` capture not run

## Package P4

### Implementer + repair

- **`client/shared/login_session/fl_product_login`** — promoted session writer + P1 wire parsers
- **Device:** `product_login_bridge`, `product_login_http_worker` (Kconfig P1 URL + desk token); `V2_PRODUCT_LOGIN_READY`
- **Tests:** `test_product_login_model` (14), `test_product_login_host` (4); LAB-07/08 model regressions OK

### Verifier

- **First pass:** fail (PIN/cooldown L1, thin device HTTP)
- **After repair:** **pass** (host L1/L2 per brief)
- **Residual gaps:** placeholder device UI (not full P2 catalog screens); HTTP worker not proven on kit; health POST `muted:false` hardcode; narrow L2 live loops

## Package P5

### Implementer + repair

- **`credential_session.cpp`**, **`workers.cpp`** — 32-byte Bearer, 60s refresh, 180s bound, invalidate spacing, generation-fenced inbox/send stubs
- **Tests:** `test_product_session_model` (10), `test_product_session_host` (4); P4 + LAB-09 model/host regressions (**49** tests in battery)

### Verifier

- **First pass:** fail → **repair:** pass
- **Hardening (operator-authorized):** [`p5-hardening-notes.md`](p5-hardening-notes.md) — send-worker L1, health ack/server_time, late focus/media, L2 credential lifecycle on P1
- **Independent verifier:** **pass** · **hardening_validated:** true · **53** tests in regression battery
- **Residual:** device kit credential HTTP not proven; server-side stale clock is contract semantics (client gates invalid acks)

## Package P6

### Implementer + repair

- **`client/shared/inbox_carousel/fl_product_inbox`** — carousel reducer, flsk2, prefix playback, P5 worker tickets
- **Device:** `product_inbox_bridge` (thin UI; HTTP worker deferred)
- **P1 server:** media **Range → 206** for L2 prefix reads
- **Tests:** `test_product_inbox_model` (11), `test_product_inbox_host` (2); repair closed recovery/60s/EXP-044/sketch-session

### Verifier

- **First pass:** fail (thin L2, recovery/sketch gaps)
- **After repair:** **pass** (host L1/L2)
- **Residual:** no device inbox HTTP loop; no L3 acoustic; carousel EXP matrix not per-row host-probed

## Package P7

### Implementer

- **`client/shared/record_capture/`** — promoted LAB-10 core + `fl_product_record` (session, attached storage, flsk2)
- **Device:** `product_record_bridge` — 240×240 + 80×240 rail, `V2_PRODUCT_RECORD_READY`
- **Tests:** `test_product_record_model` (5), `test_product_record_scripted` (1); LAB-10 **71** py tests OK

### Verifier

- **Verdict:** **pass** (host L1/L2)
- **Gaps:** COM-SCN-019 partially asserted; device touch→capture not wired; upload is P8

## Package P7

### Implementer

- **`client/shared/record_capture/`** — promoted LAB-10 ui/capture/render; `fl_product_record` session, attached storage, flsk2 encoder; product ink-overflow (no auto-finish)
- **Device:** `product_record_bridge` (240×240 canvas + 80×240 rail); `product_shell_stage()`; `V2_PRODUCT_RECORD_READY`
- **Tests:** `test_product_record_model` (5), `test_product_record_scripted` (1); LAB-10 python suite unchanged (71)

### Verifier

- **Verdict:** pass (host L1/L2)
- **Residual:** device touch→capture loop not wired; no L3 kit capture; auto-start on record stage is harness-only

## Package P8

### Implementer

- **`client/shared/message_delivery/fl_product_send`** — outbox reducer (16 KiB parts), COM-013/014, picker/journey; P1 wire parsers
- **Device:** `product_send_bridge` (preparing/sending/sent copy); `V2_PRODUCT_SEND_READY`
- **Tests:** `test_product_send_model` (12), `test_product_send_host` (2)

### Repair + verifier

- **Notes:** [`p8-hardening-notes.md`](p8-hardening-notes.md) — LAB-11 manifest/ws2_32, COM-013 malformed wait, mute-after-finish gate
- **Verdict:** **pass** (33-test send + LAB-11 battery)
- **Residual:** device send HTTP worker; P7→outbox on device deferred to **P10**

## Package P9

### Implementer + repair

- **`client/shared/desk_volume/fl_product_volume`** — 13-stop table, persist, BOOT journey, speaker callback hook
- **Device:** `product_volume_bridge`, `product_hardware_buttons` → BOOT/mute/red; NVS persist
- **Tests:** `test_product_volume_model` (**10**); [`p9-hardening-notes.md`](p9-hardening-notes.md) EXP-001–017 map

### Verifier

- **First pass:** fail → **repair:** pass (host L1/L2)
- **L3:** EXP-009 speaker on kit waived per brief

## Package P10

### Implementer

- **`client/shared/product_journey/`** — `HostJourney` + seven LAB-13 overlap L1 scenarios
- **`scripts/product_journey/run_l1.py`** — host L1 runner (no HTTP)
- **`scripts/tests/test_product_journey_host.py`** — L1 battery + P1 L2 roster/PIN, inbox+Range 206, send receipt
- **`docs/plans/v2-experience/p10-journey-manifest.json`** — steps, evidence L0/L1/L2, **resource_peaks: unmeasured**
- **Device:** `product_stage_router` — signed-in → inbox stage; red → record when not in volume

### Verifier

- **Verdict:** pass (host L1/L2) — 4 journey host + 7 L1 overlaps + 28-test regression battery
- **Residual:** device composed L2 waived (no kit); P7 record not in `HostJourney`; boot-during-send overlap is volume/red only; `resource_peaks: unmeasured`; stage router L0 not host-probed
