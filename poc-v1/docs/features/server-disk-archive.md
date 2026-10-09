# Feature: Server disk message archive

| Field | Value |
|-------|-------|
| **Status** | **Verified** (21 unit tests including crash/restart simulations) |
| **Areas** | Server only |
| **Last updated** | 2026-10-08 |

## Purpose

Persist complete messages, optional sketches, and per-user mutable state on disk so restart, backup, and single-writer semantics are predictable for the household server.

## UX

- Operators see no direct UI — behavior is invisible except faster restart and no “empty inbox after reboot.”
- Migration gate: server **refuses** to start over legacy blob folders without export (`README`).

## Behavior

- One `MessageArchive` per `message_store` with file lock.
- Commits: manifest + `media.wav` + `complete` marker; incoming PCM chunks until complete.
- Startup verifies media hashes; corruption fails closed with path in error.
- SQLite holds read/position/profile/PIN overrides separate from immutable manifests.
- Broadcast shares one media file; multipart and chunk paths converge on same layout.

## Implementation

| Layer | Location |
|-------|----------|
| Archive | `demos/server/v1_product/archive.py` |
| Integration | `user_mailbox.py` when `archive=` passed |
| Chunk API | `chunk_api.py` |
| Docs | [`demos/server/v1_product/README.md`](../../demos/server/v1_product/README.md) |

## Constraints

- Single worker/process per archive.
- No automatic deletion of old messages.
- Windows directory fsync qualification called out separately in README.
- Live migration from old memory-based server **blocked** until export/import tooling exists.

## Verification

```bash
python -m unittest discover -s demos/server/v1_product/tests -v
```

Includes: process crash boundaries, chunk resume, second server lock, disk full rollback, bounded memory on large finalize.

## Remaining Work

- Qualified import path from legacy mailbox metadata.
- PIN-override persistence beyond SQLite (called out as future in README).
- Production backup runbook cross-link (evidence under `docs/evidence/product-no-storage/`).
