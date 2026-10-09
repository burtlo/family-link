# Feature: PIN auth and user session

| Field | Value |
|-------|-------|
| **Status** | **Implemented** (server routes + firmware UX); **Partial** verification — unit tests use session headers but do not exercise `POST /v1/session/login` or admin PIN reset |
| **Areas** | Server, firmware, web admin |
| **Last updated** | 2026-10-08 |

## Purpose

Let any hangout member sign in on any endpoint with a per-user PIN, obtain a server-backed session (inbox + profile), and recover from Lynn-initiated PIN resets without exposing the new PIN on the glass.

## UX

- **Signed out:** Roster of hangout users from `GET /v1/hangout`.
- **PIN pad:** Digits 1–9; shoulder clears or returns to roster; wrong PIN feedback; five failures → 60 s lockout (`shared/v1/timing.yaml`).
- **After login:** Carousel; optional toast when `pin_reset` was true (`ask Lynn` flow per spec).
- **Idle:** One-minute carousel idle returns to PIN lock (firmware).
- **Admin:** Lynn sets PIN via web; box never displays the new digits.

## Behavior

- Endpoint must present valid **device Bearer token** for all API calls.
- `POST /v1/session/login` verifies PIN against registry hash or server-stored override after admin reset.
- Successful login returns full **inbox payload** plus `pin_reset` flag; server clears one-shot reset flag on success.
- Session identity on subsequent calls: `X-User-Id` header.
- PIN gates **carousel and recording** (spec); firmware enforces via state machine.

## Implementation

| Layer | Location |
|-------|----------|
| Server login / PIN | `demos/server/v1_product/server.py`, `demos/server/_shared/user_mailbox.py` |
| Admin reset | `POST /v1/admin/pin-reset` |
| Firmware PIN | `firmware/v1/v1_auth.c`, async worker in `v1_auth_login_task` |
| HTTP login | `firmware/v1/v1_api.c` → `/v1/session/login` |
| Admin UI | `demos/parent/web/v1.js` |

## Constraints

- Web admin password is **not** the box PIN (separate credentials in hangout YAML).
- Admin tokens are in-memory only (lost on server restart).
- INT-014 class bugs: PIN `checking...` must not hang forever — network failure must route to connecting (`v1_auth.c`, BOX-UI).

## Verification

- Server: send/read/state paths with fixed `X-User-Id` in `demos/server/v1_product/tests/test_api.py`. Login and PIN-reset routes exist in `server.py` but lack dedicated unit tests; optional smoke: `demos/server/v1_product/client.py`.
- Firmware: manual — [`device-test-after-flash`](../../.cursor/skills/device-test-after-flash/SKILL.md); PIN → carousel or `wrong pin` within ~3 s.

## Remaining Work

- Treat HTTP 5xx during login as **connecting**, not `wrong pin` (operational-contract gap).
- Persist admin sessions or document restart impact if needed for ops.
- No automated firmware PIN regression in `tests/`.
