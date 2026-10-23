# Orchestration progress — v2-experience

Status: **in progress** (P1–P4 complete)

| Package | Implement | Verify | Notes |
|---------|-----------|--------|-------|
| P1 | completed (+ hardening) | pass (independent) | 12 host tests; `p1-hardening-notes.md` |
| P2 | completed (+ hardening) | pass (independent) | CAT-21–CAT-37; `p2-hardening-notes.md` |
| P3 | completed | pass | `client/shared/stage_shell/`; `V2_PRODUCT_SHELL_READY` |
| P4 | completed | pass (host L1/L2) | `client/shared/login_session/`; P1 wire; device login mount |

## Package P1

### Implementer (initial)

- `server/labs/lab-p1-core-journey-wire/` + `scripts/tests/test_lab_p1_host.py`.

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
