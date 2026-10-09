# Family Link product server on your computer

This is the server used by X02 (`firmware/v1/`). Run this module, rather than
`demos.server.combined.server` or the H34 experiment server.

## Storage implemented on 2026-10-07

- Complete audio, optional existing FLSK1 drawings, sender/recipient metadata,
  timestamps, media hashes and per-recipient sequence identities are stored on disk.
- Inbox metadata is rebuilt from committed manifests after a restart. Audio is
  not retained in Python memory. A small derived metadata index stays in memory.
- Read flags, playback positions, last-viewed position and profiles are stored in
  SQLite, separately from immutable message manifests.
- Existing device multipart sends still work. PCM chunk uploads can resume after
  a restart; they become visible only after whole-message completion.
- Broadcasts share one media file and commit the entire recipient set together.
- Media endpoints stream files and support byte ranges.
- No automatic archive expiration or deletion. Older messages remain on disk.

Removable device storage is paused pending supported cards. This server works
without it. The firmware does **not yet use these chunk endpoints**; it currently
uploads WAV multipart requests. Server support alone does not fix the device's
25-second sending screen or provide a reboot-safe device outbox.

## Data location

The default is outside the source checkout:

| Computer | Default data folder |
|---|---|
| Windows | `%LOCALAPPDATA%\Family Link` |
| macOS | `~/Library/Application Support/Family Link` |
| Linux | `$XDG_DATA_HOME/family-link`, or `~/.local/share/family-link` |

Set `FAMILY_LINK_DATA_DIR` to choose another local folder. Set
`FAMILY_LINK_MESSAGE_STORE` to override just the archive folder (for example on
another local disk). `FAMILY_LINK_ROOT` selects application assets; it no longer
selects the product's persistent data location.

Set `FAMILY_LINK_HANGOUT_REGISTRY` to your private hangout YAML path. Otherwise
`hangout.local.yaml`, then `hangout.example.yaml`, is loaded from the checkout.
Real endpoint tokens, PINs and web passwords belong in the private file.

```text
<data folder>/message_store/
  .server.lock                       # one server process owns this archive
  incoming/<uuid>/
    upload.json                      # frozen sender/targets/chunk receipts
    audio/000000.chunk               # durable raw PCM chunks
  messages/<year>/<month>/<uuid>/
    manifest.json                    # immutable metadata + recipient seqs + hashes
    media.wav                        # canonical completed audio
    sketch.flsk                      # optional existing bounded FLSK1 drawing
    complete                         # completion marker
  state/user-state.sqlite3            # mutable read/position/profile state
```

`incoming` is retained across disconnects and restarts. It is not an inbox.
Acknowledged chunks are never held only in memory. Completion assembles media in
64 KiB blocks, fsyncs it, and renames the message directory on the same filesystem.
Only then are inbox entries published and notifications attempted. Canonical
files replace the incoming audio chunks; a crash may leave redundant chunks,
which do not change the committed message.

## Start on Windows (PowerShell)

From the source checkout, after installing Python:

```powershell
py -m venv .venv
.\.venv\Scripts\python.exe -m pip install -r requirements-server.txt
$env:FAMILY_LINK_DATA_DIR = Join-Path $env:LOCALAPPDATA 'Family Link'
$env:FAMILY_LINK_HANGOUT_REGISTRY = (Resolve-Path .\hangout.local.yaml).Path
.\.venv\Scripts\python.exe -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
```

These environment settings apply to that terminal session. Save them in the
server's startup configuration when installing it as a background service.

## Start on macOS/Linux

```sh
python3 -m venv .venv
.venv/bin/python -m pip install -r requirements-server.txt
export FAMILY_LINK_HANGOUT_REGISTRY="$PWD/hangout.local.yaml"
.venv/bin/python -m demos.server.v1_product.server --host 0.0.0.0 --port 8080
```

Use `FAMILY_LINK_DATA_DIR` if you prefer another location. The box preview is at
`http://localhost:8080/box/`; the parent interface is at `/app/`.
The existing TLS/Tailscale deployment guidance in [TLS.md](../../../docs/TLS.md)
still applies for remote households. A local test does not qualify remote access.

Run **one server process / one worker** per archive. Do not use Uvicorn reload
against household data. A second process opening the same archive is rejected.

## Existing server data: migration gate

Do not restart an old memory-based server before preserving its live inbox
metadata. Old WAV files alone do not reveal their sender, timestamp, read state
or the original per-user sequence mapping.

The new server refuses to seed over legacy `blobs`, `shared`, `sketches` or
`shared_sketches` in its configured data folder. It also detects the checkout's
old product media when a new archive would otherwise be started silently.
No live household server or data was migrated by this implementation.

For a deliberately separate, **empty test installation**, set
`FAMILY_LINK_START_FRESH=1`. This bypasses the checkout-legacy warning, leaves old
files intact, and does not import them. It cannot bypass the guard against
legacy files in the selected new data folder. Do not treat it as migration.

Deployment over existing data remains blocked until a live metadata export and
verified import into a separate archive are available. Preserve the old files
and process; never infer missing identities from filenames or overwrite them.

## API compatibility and PCM chunks

`POST /v1/messages` dispatches by content type:

- Existing multipart: `kind=audio`, `to_user_id` or `broadcast`, `blob` WAV,
  optional `sketch` FLSK1, optional UUID `client_message_id`. Complete media is
  copied from the upload spool in bounded blocks. Response keeps `messages` and
  adds each logical `message_id`.
- JSON: `family-message/1` create with `audio.codec=pcm_s16le`, 16 kHz, mono,
  recipient or broadcast and optional UUID client identity. The response gives
  the message UUID and supported limits. Unsupported codecs/sketch schemas fail
  explicitly; they are not silently converted.

Chunk routes follow [MESSAGE-PROTOCOL.md](../../../docs/MESSAGE-PROTOCOL.md):

```text
PUT  /v1/messages/<uuid>/audio/<sequence>
GET  /v1/messages/<uuid>/upload
POST /v1/messages/<uuid>/complete
GET  /v1/messages/<uuid>/audio
GET  /v1/outgoing/<client-uuid>
```

Authenticate with the existing endpoint Bearer token and signed-in `X-User-Id`.
Open-upload/status/completion routes belong to the sender; completed audio
belongs to recipients. The legacy recipient `/{seq}/blob` and `/{seq}/sketch`
routes continue to work. Upload creation freezes the broadcast recipient set.
Same sender/client UUID and create metadata recover the original upload; same
chunk sequence/bytes/hash/timing recover the original chunk receipt. Conflicts
return 409. Identical completion retries return the original recipient seqs.

PCM chunk limits: 192 KiB, 5 seconds, 450 chunks, 180 seconds total, 6 MiB total.
At the normal 2-second cadence a three-minute message uses 90 chunks. Completion
requires contiguous sequences/timing and an aggregate SHA-256. Chunk request
bodies stream to temporary disk files; neither requests nor final assembly join
whole messages in application memory. Multipart requests have a 7 MiB total body
cap and two simultaneous media transfers. Login traffic does not wait for those
transfer slots. A default 64 MiB free-disk floor rejects writes with 507; configure
`FAMILY_LINK_FREE_DISK_FLOOR_BYTES` to change it.

The PCM chunk subset currently omits Opus, FLSK2, abort/culling endpoints and
automatic cleanup of abandoned uploads. Existing FLSK1 drawing uploads remain
supported through multipart. Admin login tokens and PIN-reset overrides remain
session state in memory; PIN-override persistence is separate future work.

Inbox responses default to the latest eight entries, oldest-to-newest within the
page, to keep normal BOX responses bounded. `?limit=1..16&before_seq=<seq>` retrieves
older pages; `has_more`, `next_before_seq` and `total_messages` describe the archive.
Current firmware does not yet expose older-page navigation. An older viewed
position may be outside its current page; no archived message is deleted.

## Recovery, backups and qualification

Startup verifies committed media hashes in bounded blocks. Corrupt/missing media
or invalid manifests stop startup with their location, preserving all bytes.
The server does not reseed, guess metadata or silently discard corruption.

For a consistent simple backup, stop the **new disk-based server**, copy the
entire archive including SQLite mutable state, and preserve the private registry
separately. Restore into a separate local folder and test there first. Do not use
this restart advice for the old memory-based server before exporting metadata.

```sh
.venv/bin/python -m unittest discover -s demos/server/v1_product/tests -v
```

See [qualification evidence](../../../docs/evidence/product-no-storage/03-server-recovery/README.md).
Tests prove process-crash/restart behavior on the development Mac, not power-loss
survival or Windows deployment. Directory fsync support is reported separately;
Windows uses file fsync and atomic rename with directory sync unqualified.
[Starlette file responses](https://www.starlette.io/responses/) provide streaming
and HTTP ranges; [SQLite transactions](https://www.sqlite.org/atomiccommit.html)
commit mutable user state.
