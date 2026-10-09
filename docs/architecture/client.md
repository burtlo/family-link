# V1 clients (firmware and web)

## Firmware (BOX-3, demo id `x02`)

**Entry:** `firmware/demos/x02_product_shell.c` → `firmware/v1/x02_main.c`.

**Modules:**

| Module | Responsibility |
|--------|----------------|
| `v1_state` | Screen state machine (roster, PIN, carousel, picker, record, settings, connecting, sleep) |
| `v1_connect` | Wi-Fi / server reachability, hangout probe, connecting UI |
| `v1_auth` | PIN pad, lockout, async `POST /v1/session/login` worker |
| `v1_api` | HTTP JSON helpers, inbox fetch, WebSocket client to `/v1/ws` |
| `v1_carousel` | Inbox cards, play/pause, scrub, `view`/`read`/`position` sync, blob download |
| `v1_record` | Recipient picker, mic capture, multipart upload, send progress UI |
| `v1_ui_common` | Shared LVGL widgets, settings, sleep/dim, profile `PUT` |
| `v1_timing.h` | Generated constants from `shared/v1/timing.yaml` |

**Upload path today:** `v1_record.c` builds **multipart/form-data** `POST /v1/messages` with WAV blob (20 s HTTP timeout; `V1_SEND_STUCK_MS` 25 s UI guard). Does **not** call JSON/chunk endpoints.

**Playback:** Streams from `GET /v1/messages/{seq}/blob` (and sketch route when present).

Build: `make x02`, flash `make flash DEMO=x02`. Timing parity: `make check-v1-parity`.

Device verification expectations: [`.cursor/rules/device-verify-before-done.mdc`](../../.cursor/rules/device-verify-before-done.mdc), [`device-test-after-flash` skill](../../.cursor/skills/device-test-after-flash/SKILL.md).

## Web twin (`/box/`)

**Location:** `demos/server/v1_product/web/` (`index.html`, `box.js`, `box.css`, `v1_timing.js`).

Browser stand-in for carousel, picker, settings, and send receipt flow. Imports shared timing from generated `v1_timing.js`. Documented in [`demos/server/v1_product/web/README.md`](../../demos/server/v1_product/web/README.md).

Uses the same REST API as firmware (endpoint token + user session headers configured in UI).

## Web admin (`/app/`)

**Location:** `demos/parent/web/v1.html` + `v1.js` (mounted at `/app` by v1 server).

Capabilities: admin login, per-user PIN reset (PIN shown only in browser log), welcome WAV upload, send audio to user or broadcast. Does not implement full inbox carousel — admin is operational, not a second mailbox UI.

Legacy combined parent UI may exist at `index.html`; v1 admin is the product path per spec.

## Client comparison

| Capability | Firmware | Web twin | Web admin |
|------------|----------|----------|-----------|
| PIN sign-in | Yes | Simulated / dev | N/A |
| Carousel inbox | Yes | Yes | No |
| Record + send | Yes (mic) | Simulated send | Upload WAV |
| PIN reset | Consume flag | N/A | Yes |
| Welcome / First Message | Receive | Receive | Upload |

Operational gaps between firmware and twin are tracked in [`plans/operational-contract.md`](../plans/operational-contract.md) (draft plan, not fully as-built).
