# Product server disk archive — 2026-10-07

## Scope and result

Implemented in the actual `demos.server.v1_product.server`, using its existing
hangout registry, endpoint authentication, recipient seq routes and web assets.
This is not the separate H34 app and does not use its global app/data/auth state.
Only H34's bounded PCM-to-WAV helper is reused.

**21 synthetic host tests pass** on the development Mac. No device was flashed,
no SD card was accessed, and no running household server was restarted/migrated.
Windows and sudden-power-loss qualification remain pending.

## Implementation surfaces

- `demos/server/v1_product/archive.py`: canonical archive, fsync/rename commit,
  checksum verification/recovery, process ownership lock, resumable uploads.
- `demos/server/v1_product/chunk_api.py`: product-authenticated PCM protocol subset.
- `demos/server/v1_product/body_limit.py`: bounded requests and media concurrency.
- `demos/server/v1_product/server.py`: multipart adapter, streamed reads,
  receipt lookup, default persistent location, legacy-data guards, admin time fix.
- `demos/server/_shared/user_mailbox.py`: optional archive adapter and SQLite
  read/position/profile persistence. Other island/combined users retain their
  existing behavior unless explicitly given an archive.
- `demos/server/_shared/hangout_registry.py`: private external registry override.

## Manifest and adapter

Schema `family-product-manifest/1` records message UUID, optional client UUID,
state/protocol, sender and label, exact frozen targets, broadcast/system flags,
creation/completion timestamps, duration, canonical audio and optional FLSK1
path/byte count/SHA-256, and recipient `{user_id,seq}` references. Chunk completion
also retains the create request, exact accepted chunk descriptors and completion
request for conflict-safe replay after restart.

The directory rename commits the entire recipient set. The in-memory inbox is a
rebuildable view. Existing recipient seq-based blob/sketch routes resolve to that
manifest's fixed canonical paths, not the old direct/shared file layout. One
broadcast media file services multiple references. SQLite stores mutable user
state and is not the source of immutable message identity/media.

## Verification

Command:

```sh
.venv/bin/python -m unittest discover -s demos/server/v1_product/tests -v
```

Coverage includes:

- Direct multipart send, full blob retrieval, 206 range and 416 invalid range.
- Restart preserving recipient seqs/IDs, profiles, read flags and positions.
- Welcome seeding idempotence; one shared broadcast WAV/FLSK1 file after restart.
- Chunk create retry, durable chunk receipt retry, ownership, conflict rejection,
  restart resume, completion and repeated completion preserving recipient seqs.
- Multipart client-ID replay and sender-owned receipt recovery after restart.
- Invalid WAV, unsupported codec, oversized body, missing chunks and wrong hashes.
- Full-disk floor rejection before publication; failed mutable-state save rollback.
- Corrupt accepted media failing startup rather than overwriting/reseeding.
- A second server process/instance rejected for the same root.
- Actual subprocess exit 86 at `chunk_file`, `chunk_ack`, `assembled`,
  `before_commit`, and `after_commit`; retry/recovery yields one complete logical
  message and its whole recipient set. Separate multipart before/after-commit
  crashes prove no partial broadcast publication.
- Actual three-minute PCM chunk finalization (90 chunks, 5,760,044-byte WAV) and
  three-minute multipart archive import, each below 1 MiB tracemalloc peak during
  archive processing. This measures Python allocation in those paths; it is not
  an end-to-end RSS claim, network benchmark or device-memory result.
- Bounded latest-eight inbox page and older-page retrieval preserving all history.
- Product websocket authentication with the configured registry, working admin
  login, notification failure not reversing a committed upload, and stalled
  websocket notifications bounded to 500 ms concurrently for the recipient set.

## Limits and deployment gates

The host tests qualify ordinary process crash/restart on macOS's local filesystem.
They do not qualify hardware power loss. Directory fsync capability is explicit;
Windows cannot use the POSIX directory-fsync path and remains unqualified until
its actual drive is tested. Use one worker and a local filesystem; network-share,
cloud-synchronized-folder and multi-process deployments are not qualified.

The device still uses multipart WAV; its RAM loss and UI freeze issues are not
resolved by this server change. Opus/FLSK2/device outbox/culling/abort and PIN-reset
persistence remain future work. Current firmware shows the current inbox page,
not the complete paginated archive.

Before switching an existing memory-based household server, preserve live
metadata and verify migration into a separate root. Legacy orphan audio does not
justify invented sender/date/sequence values. Default-root legacy guards require
that gate or a deliberate separate fresh installation. No automatic migration,
legacy deletion, production restart or claim of deployed Windows operation was
performed here.
