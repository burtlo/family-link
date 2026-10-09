# Plan: h34 message_store demo

| Field | Value |
|---|---|
| **Doc kind** | `island-demo` |
| **Status** | `done` (host PCM island) |
| **Last updated** | 2026-10-05 |

## Goal

Prove Phase 3 canonical server storage on the host using the PCM fallback path from [`MESSAGE-PROTOCOL.md`](../MESSAGE-PROTOCOL.md) and the directory contract in [`SERVER-MESSAGE-STORAGE.md`](../SERVER-MESSAGE-STORAGE.md).

## Deliverables

- `demos/server/h34_message_store/` — create, chunk PUT (stream to `work/*.part`), upload status, complete, streaming GET with Range, inbox listing for completed messages only.
- Restart survival without wiping `data/v1_product/message_store/` on boot.
- Administrator cull preview, trash move, and restore (hash-stable media).
- Evidence: [`evidence/h34-message-store/README.md`](../evidence/h34-message-store/README.md).

## Out of scope

- Opus/Ogg finalization (see **h31**).
- Firmware / x02 integration.
- SQLite derived index (optional in product; JSONL inbox only here).

## Acceptance (host)

Matches Phase 3 bullets in [`long-message-experiments.md`](long-message-experiments.md) where feasible without device: no full-payload RAM on receive, restart preserves open and completed state, duplicate/conflict chunk responses, complete marker + manifest under `messages/YYYY/MM/`.

## Result and remaining work

The [h34 correction plan](h34-message-store-corrections.md) is complete for its defined host PCM scope. Its [qualification evidence](../evidence/h34-message-store/README.md) records a deterministic 180-second upload and WAV, 21/21 passing checks, 50/50 process-crash boundaries, bounded server allocations, Range playback, and restart-safe cull/restore. Tested source revision: `d0f70c42781ecb433dd7d624ce016cf87b1db955`.

Opus/Ogg, sketches, upload abort, notifications, device storage/outbox/playback, and X02 integration remain in the [long-message roadmap](long-message-experiments.md). Sudden power-loss behavior is not established by the host process-crash campaign.
