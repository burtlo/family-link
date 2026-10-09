# Feature: Web admin parity

| Field | Value |
|-------|-------|
| **Status** | **Verified** (admin login, send, restart persistence in unit tests); **Implemented** (PIN reset — route + UI, not covered by `tests/`) |
| **Areas** | Server, `demos/parent/web` |
| **Last updated** | 2026-10-08 |

## Purpose

Give Lynn (web-admin user) household operations without using a phone app: authenticate separately from box PINs, reset user PINs, seed or replace welcome audio, and send async audio to one user or everyone.

## UX

- Open `/app/v1.html` against the v1 server origin.
- Login panel → admin panel with PIN reset fields, welcome WAV picker, send form (user + broadcast).
- Log area shows outcomes (including **new PIN in clear** for out-of-band tell).

## Behavior

- `POST /v1/admin/login` matches `web_admin` user by id or lowercased name + `web_password` from registry YAML.
- Returns bearer token (~1 h TTL in server memory).
- `POST /v1/admin/pin-reset` — sets PIN override + `pin_reset` flag for next box login.
- `POST /v1/admin/welcome` — imports system WAV, fans out First Message style seed to all users.
- `POST /v1/admin/messages` — sends as admin user id via same archive path as device multipart.

## Implementation

| Layer | Location |
|-------|----------|
| Routes | `demos/server/v1_product/server.py` (`/v1/admin/*`) |
| UI | `demos/parent/web/v1.html`, `v1.js` |
| Static mount | `_mount_static()` → `/app` |

## Constraints

- Does not expose full carousel admin — operational send/reset only.
- Endpoint registry editing remains YAML file edit (spec: config on disk).
- Not a substitute for TLS/Tailscale deployment docs.

## Verification

- `test_admin_login_and_notification_failure_still_accepted` — login, send, survives WS failure and server restart.

## Remaining Work

- Richer admin UX (user list from hangout, audit log).
- Secure storage for web passwords (currently plain in private YAML).
- Link plan **As-built** field in `v1-product-spec.md` to this feature record.
