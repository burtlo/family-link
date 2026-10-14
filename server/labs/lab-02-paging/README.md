# LAB-02 Phase B — inbox paging server

Versioned **lab fixture** server for `fl.inbox-page/2` (subset used by LAB-02 firmware). Not product inbox policy.

## Run

From repo root:

```bash
make lab.lab02.server
# or: python server/labs/lab-02-paging/server.py --port 8765
```

Defaults: listen `0.0.0.0:8765`, seed inbox from `client/labs/lab-02-bounded-inbox/fixtures/inbox_64.json`.

## HTTP API

| Method | Path | Purpose |
|--------|------|---------|
| GET | `/v2/inbox?cursor=<offset>&limit=<n>` | Paged inbox (`fl.inbox-page/2`) |
| GET | `/lab/health` | `{"ok":true}` |
| POST | `/lab/script` | JSON body runs a lab script (see below) |

### Lab scripts (`POST /lab/script`)

```json
{"name": "delay_ms", "ms": 800}
{"name": "push_item", "sender_name": "Lab", "preview": "Pushed during snap"}
{"name": "remove_at", "index": 3}
{"name": "reset"}
```

`delay_ms` applies to the **next** `GET /v2/inbox` only.

Scripts change server state only. The device **polls page 0 about every 5s** (no push). Watch the top ribbon `focus/total` or scroll after a `push_item`.

## Device firmware

1. menuconfig **LAB-02 → Phase B paging server URL** (e.g. `http://192.168.1.42:8765`).
2. Set **Wi-Fi SSID** and **password** on the same menu.
3. Build the managed HTTP image: `make lab.lab02.build KIT=<nickname>`.
4. Start this server on the host, then run `make lab.lab02 KIT=<nickname>`.

Scenarios: `S-page-snap`, `S-rm-focus`, `S-push` — see [lab-02 implementation outline](../../../docs/labs/lab-02-implementation-outline.md).
