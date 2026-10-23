# P2 hardening notes

- **Date:** 2026-10-23
- **Package:** P2 (device UI catalog + screen inventory)

## Changes

- Refreshed [`screens/MISSING.md`](../../product/device-ui/screens/MISSING.md) for P2-authored `SCR-*` docs and `CAT-21` … `CAT-37`; clarified connecting / `no Wi-Fi` are not separate screens; noted `SCR-SEND-FEEDBACK` supersedes progress/result split.
- **`CAT-23`** ([`message-inbox-empty.yaml`](../../product/device-ui/catalog/scenes/message-inbox-empty.yaml)): upper slot now `PANEL-HEADER-MESSAGE-SENDER` (`identity: Messages`, `transport: hidden`) per [`SCR-MESSAGE-INBOX`](../../product/device-ui/screens/SCR-MESSAGE-INBOX.md).
- Re-packed device UI catalog (`validate` + `pack`).

## Verification

```bash
.venv/bin/python scripts/device_ui_catalog.py validate
.venv/bin/python scripts/device_ui_catalog.py pack
.venv/bin/python -m unittest scripts.tests.test_device_ui_catalog -q
```
