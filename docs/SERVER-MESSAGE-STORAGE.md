# Server message storage

## Goal

Keep every completed message in one predictable filesystem directory, use bounded server memory, recover after restart, and support explicit administrator culling later. The filesystem is canonical; an index may accelerate queries but must be rebuildable from message manifests.

## Root selection

The server root remains configurable through `FAMILY_LINK_ROOT`. Message storage SHOULD live at:

```text
<root>/data/v1_product/message_store/
```

Production may point the message-store root at another disk through a dedicated configuration value. Code must not depend on the repository being the permanent production location.

## Directory contract

```text
message_store/
  incoming/
    <message-id>/
      manifest.json
      audio/
        000000.chunk
        000001.chunk
      sketch/
        000000.flsk2
      work/

  messages/
    2026/
      10/
        <message-id>/
          manifest.json
          audio.wav
          sketch.flsk2
          complete

  trash/
    2026/
      10/
        <message-id>/
          manifest.json
          audio.wav
          sketch.flsk2
          deletion.json

  state/
    inboxes.jsonl
    playheads.jsonl
    admin-audit.jsonl
    index.sqlite3        # optional derived index
```

The year/month partition prevents one directory from accumulating an unbounded number of entries. The message ID remains the stable lookup key. A derived index maps IDs to paths.

## Completed manifest

`manifest.json` is the durable source of message metadata. It SHOULD include:

```json
{
  "schema": "family-message-manifest/1",
  "message_id": "server-uuid",
  "client_message_id": "optional-device-uuid",
  "state": "complete",
  "from_user": "mazi",
  "recipients": ["lynn"],
  "broadcast": false,
  "created_at": "2026-10-04T18:00:00Z",
  "completed_at": "2026-10-04T18:01:14Z",
  "duration_ms": 73420,
  "audio": {
    "codec": "pcm_s16le",
    "container": "wav",
    "path": "audio.wav",
    "bytes": 2349484,
    "sha256": "…",
    "sample_rate_hz": 16000,
    "channels": 1
  },
  "sketch": {
    "format": "flsk2",
    "path": "sketch.flsk2",
    "bytes": 8192,
    "sha256": "…",
    "canvas_width": 320,
    "canvas_height": 240
  },
  "closed_reason": "button",
  "protocol": "family-message/1"
}
```

Paths inside a manifest are relative to that message directory. Unknown optional fields are preserved when rewriting a manifest.

## Incoming writes

For every received chunk:

1. Validate message ownership, state, declared size, and configured limits.
2. Stream the request body to `work/<random>.part` while calculating SHA-256 and counting bytes.
3. Flush and close the file.
4. Compare actual size and checksum with the request.
5. Atomically rename it to its sequence path.
6. Atomically update the incoming manifest or append a durable receipt journal.
7. Return the durable acknowledgement.

Never acknowledge a chunk while it exists only in Python memory. Never use the client's original filename as a filesystem path.

## Finalization

PCM finalization writes a WAV header followed by audio chunk contents in sequence to a temporary output. It does not join chunks in RAM. After validating size, duration, and aggregate checksum:

1. Write the completed manifest into the incoming directory.
2. Create the zero-length `complete` marker last.
3. Flush relevant files and directory metadata where supported.
4. Atomically rename the whole directory into its year/month location under `messages/`.
5. Append recipient inbox references.
6. Emit notifications.

Chunk files SHOULD be removed after canonical media is safely committed so storage is not doubled. They MAY be retained briefly for diagnostics under a configured development option.

Opus finalization will be selected by the Opus demo. It should produce a standard seekable file rather than a private concatenation of packets.

## Restart recovery

At startup the server scans:

- `messages/**/complete`: validate minimal manifest fields and rebuild the derived index.
- `incoming/`: make open uploads available for status queries; mark stale ones abandoned rather than deleting immediately.
- `trash/`: rebuild the administrator trash view and purge eligibility.

Corrupt or incomplete completed directories are quarantined and reported. They are not silently discarded.

The current in-memory `UserMailbox` model does not provide this persistence. Migration should make inbox references and message manifests durable before old blob layouts are retired.

## Inbox and mutable state

Immutable media and its manifest live in the message directory. Mutable per-user state lives separately:

- Inbox sequence and message ID
- Read/unread state
- Playback position
- Archived/hidden state

JSONL is acceptable for an isolation demo. SQLite is the stronger product index because it provides atomic updates and efficient administrator queries. SQLite remains an index and mutable-state store; completed media files remain directly inspectable and recoverable from manifests.

Broadcast delivery creates multiple inbox references to one message ID. Deleting one recipient's reference does not remove canonical media while another reference remains.

## Streaming reads

Media endpoints use file streaming or framework file responses. They must not call `read_bytes()` on complete media.

Requirements:

- Verify authorization before opening the file.
- Serve only directories containing a valid completion marker.
- Supply stable `ETag`, `Content-Length`, and media type.
- Support a single byte range for resume and seek.
- Align or reject invalid PCM sample boundaries.
- Keep manifest and media paths inside the configured store root.

See [STREAMING-PLAYBACK.md](STREAMING-PLAYBACK.md) for device behavior.

## Administrator culling

The desired behavior is explicit administrator cleanup rather than silent time-based deletion.

Suggested API shape:

```text
POST /v1/admin/messages/cull/preview
POST /v1/admin/messages/cull
GET  /v1/admin/messages/trash
POST /v1/admin/messages/{id}/restore
DELETE /v1/admin/messages/{id}/purge
```

A cull selector may include:

- Created before timestamp
- Completed before timestamp
- Sender or recipient
- Read by all recipients
- Archived by all recipients
- Maximum bytes to reclaim
- Explicit message IDs
- Exclusion of system or protected messages

Preview returns exact message IDs, count, total bytes, and affected inbox references. Cull then verifies that selection token before mutation so the administrator approves the same set that was previewed.

Culling procedure:

1. Record an audit entry without audio content.
2. Remove or tombstone inbox references transactionally.
3. Atomically move unreferenced message directories to `trash/<year>/<month>/`.
4. Write `deletion.json` with actor, timestamp, selector, original path, and purge eligibility.
5. Allow restore during the configured grace period.
6. Permanently purge only through an explicit action or an administrator-approved trash policy.

The final retention duration and trash grace period remain product policy decisions.

## Capacity and guardrails

- Enforce the message and chunk limits in [MESSAGE-PROTOCOL.md](MESSAGE-PROTOCOL.md).
- Configure a free-disk floor. Reject new chunks with `507` before crossing it.
- Bound open messages and concurrent writes per endpoint.
- Track bytes in `incoming`, `messages`, and `trash` separately.
- Surface the oldest incomplete upload and largest messages in administrator diagnostics.
- Back up the entire message-store root or snapshot it consistently with its SQLite index.

## Legacy migration

Current v1 files under `data/v1_product/blobs`, `shared`, `sketches`, and `shared_sketches` can be imported by a future one-time tool:

1. Walk existing inbox metadata while the legacy server is available.
2. Generate one message ID per unique direct blob or broadcast ID.
3. Copy media into the new directory structure.
4. Write manifests and inbox references.
5. Verify byte counts and hashes.
6. Leave legacy files untouched until operator review.

Migration is not part of the experiments in this documentation set.
