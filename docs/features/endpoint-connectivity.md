# Feature: Endpoint connectivity and push

| Field | Value |
|-------|-------|
| **Status** | **Partial** — hangout probe + WS hello **Implemented**; full operational matrix **Planned** (draft operational-contract plan) |
| **Areas** | Firmware, server, web twin |
| **Last updated** | 2026-10-08 |

## Purpose

Keep signed-out boxes waiting calmly for the household server, maintain endpoint WebSocket for inbox hints, and separate Wi-Fi loss from server boot delays.

## UX

- **Connecting (signed out):** Gold mailbox, `connecting` copy, three-dot animation; retry every 5 s (timing yaml).
- **No Wi-Fi:** Distinct screen (not connecting).
- **Signed in + server loss:** Offline ribbon; block send/PIN-dependent server ops; not full connecting takeover.
- **PIN checking:** Network fail → connecting, not infinite `checking...`.

## Behavior

- `GET /v1/hangout` probe with configurable HTTP timeout (`v1_connect.c`, `v1_api.c`).
- WebSocket: `hello` with endpoint token → `hello_ok`; server `notify_inbox` after message commit.
- Send path does not block > ~3 s on stalled WS (`test_stalled_notification_has_bounded_response`).

## Implementation

| Layer | Location |
|-------|----------|
| Connect UI | `firmware/v1/v1_connect.c`, `v1_ui_common.c` |
| WS client | `firmware/v1/v1_api.c` |
| WS server | `demos/server/v1_product/ws.py` |
| Twin | Partial — not all failure classes mirrored |

## Constraints

- Do not show roster tiles while offline (spec) even if NVS cache exists.
- TLS/SNTP requirements when `DEMO_SERVER_TLS` enabled.

## Verification

- Server: `test_websocket_uses_product_registry`, notification timeout test.
- Firmware: manual INT-014 acceptance (BOX-UI line ~313).
- Timing: `make check-v1-parity`.

## Remaining Work

- Publish [`OPERATIONAL-CONTRACT.md`](../plans/operational-contract.md) Phase 1 matrix as-built.
- Twin parity for connect/PIN/offline toasts.
- Confirm firmware inbox refresh on WS `inbox` events vs polling-only.
