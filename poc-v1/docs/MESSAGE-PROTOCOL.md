# Family message protocol v1

## Status and scope

This is the normative contract for durable asynchronous audio messages and synchronized sketches between X02-class devices and the v1 server. Keywords **MUST**, **MUST NOT**, **SHOULD**, and **MAY** are requirements for future implementations.

The protocol supports interrupted recording uploads, restart recovery, one-to-one and broadcast delivery, streaming playback, and later codec changes. Live talk uses a different protocol.

## Media baseline

| Field | Preferred message format | Required fallback |
|---|---|---|
| Audio codec | `opus` | `pcm_s16le` |
| Sample rate | 16,000 Hz | 16,000 Hz |
| Channels | 1 | 1 |
| Encode profile | 16 kbps VOIP; 24 kbps AUDIO optional | 256 kbps PCM byte rate |
| Codec frame | 20 ms / 320 samples | Complete 16-bit samples |
| Target chunk duration | 2,000 ms | 2,000 ms |
| Maximum chunk duration | 5,000 ms | 5,000 ms |
| Maximum message duration | 180,000 ms | Same until product policy changes |
| Maximum encoded audio | 6 MiB | 6 MiB protocol guard; expected Opus is much smaller |
| Maximum audio chunk | 192 KiB | 192 KiB |
| Maximum sketch chunk | 64 KiB | 64 KiB |
| Maximum sketch per message | 512 KiB | 512 KiB |

Opus is approved by the completed h30/h31 island proof in [plans/opus-demo.md](plans/opus-demo.md). An Opus chunk is a sequence of 20 ms packets. Each packet is preceded by a two-byte unsigned little-endian packet length. Packet length is positive and MUST NOT exceed 400 bytes. The server parses these frames and finalizes them into Ogg Opus without loading the full message into RAM.

PCM chunks contain raw samples and do not repeat WAV headers. The server creates a canonical WAV file during finalization. Codec selection does not change message identity, chunk acknowledgement, retry, or outbox semantics.

Approved Opus profile identifiers:

| Identifier | Encoder application | Target bitrate | Use |
|---|---|---:|---|
| `voip_16k` | VOIP | 16,000 bps | Default voice message profile |
| `audio_24k` | AUDIO | 24,000 bps | Optional measured higher-quality profile |

Opus encoding and decoding MUST run on a dedicated task with a product-measured stack budget. The island proof required a 32 KiB task stack and added about 180 KiB to the simpler demo binary. The 2026-10-04 [X02 + Opus partition feasibility](plans/x02-opus-partition-feasibility.md) spike measured **+181,536 B** for full X02 with retained Opus (**1,665,808 B** image vs **1,536,000 B** `SINGLE_APP_LARGE` slot — **hard overflow**). Product Opus enablement requires an enlarged application slot. Both corrected **2.125 MiB single-factory** and **dual-OTA** candidates have tool-validated geometry but are not device-proven; the production choice remains open pending a decision about physical recovery versus remote update and rollback. Margin at either candidate app slot is **334,234 B**, not the current slot’s **262,144 B**. Evidence: [`docs/evidence/x02-opus-partition/`](evidence/x02-opus-partition/), corrections [`phase4-corrections.md`](evidence/x02-opus-partition/phase4-corrections.md).

## Identity

- The server generates a UUID message ID when reachable.
- A device MAY begin offline with a UUID provisional ID generated from a cryptographically strong random source.
- Message IDs are opaque lowercase strings in APIs. Clients MUST NOT derive authorization or recipient information from them.
- An authenticated endpoint supplies the sending user through the existing signed-in session header.
- A message declares either one `to_user_id` or `broadcast: true`.
- The canonical media exists once. Per-user inbox records reference the message ID and retain their own sequence, read state, and playhead.

## Message states

```text
local-only → open → finalizing → complete → trashed → purged
                ↘ abandoned
```

- `local-only`: durable on the device but not yet known to the server.
- `open`: server accepts idempotent chunks.
- `finalizing`: completion was requested; server is validating or assembling.
- `complete`: immutable media is visible to recipients.
- `abandoned`: incomplete upload retained temporarily for recovery or inspection.
- `trashed`: removed from inboxes and moved to recoverable server trash by an administrator.
- `purged`: media and metadata permanently removed after an explicit administrative action or approved trash policy.

Only `complete` messages may appear in inboxes or media playback routes.

## Create a message

`POST /v1/messages`

```json
{
  "protocol": "family-message/1",
  "client_message_id": "optional-offline-uuid",
  "to_user_id": "lynn",
  "broadcast": false,
  "audio": {
    "codec": "opus",
    "profile": "voip_16k",
    "sample_rate_hz": 16000,
    "channels": 1,
    "frame_ms": 20,
    "packet_framing": "u16le_length",
    "target_chunk_ms": 2000
  },
  "sketch": {
    "format": "flsk2",
    "canvas_width": 320,
    "canvas_height": 240
  }
}
```

The response is `201 Created` for a new message or `200 OK` when an identical `client_message_id` already belongs to the authenticated sender.

```json
{
  "message_id": "server-uuid",
  "client_message_id": "optional-offline-uuid",
  "state": "open",
  "limits": {
    "max_duration_ms": 180000,
    "max_audio_chunk_bytes": 196608,
    "max_audio_bytes": 6291456,
    "max_opus_packet_bytes": 400,
    "max_sketch_chunk_bytes": 65536,
    "max_sketch_bytes": 524288
  }
}
```

The server MUST return the same message ID when the same authenticated sender retries a create request with the same client ID and equivalent immutable metadata. Conflicting metadata returns `409 Conflict`.

## Upload an audio chunk

`PUT /v1/messages/{message_id}/audio/{sequence}`

The request body is the encoded chunk. Required headers:

```text
Content-Type: application/octet-stream
X-Chunk-SHA256: lowercase hexadecimal SHA-256
X-Chunk-Start-Ms: non-negative integer
X-Chunk-Duration-Ms: positive integer
X-Chunk-Bytes: non-negative integer
```

Rules:

- Sequence numbers start at zero and increase without gaps in the finalized message.
- `X-Chunk-Start-Ms` MUST be monotonic and SHOULD equal the sum of prior audio durations.
- PCM chunks MUST contain complete 16-bit mono samples.
- Opus chunks MUST contain only complete `uint16_le length + packet` records. A truncated length or packet is a validation error.
- Opus defaults to 16 kbps VOIP. A sender using 24 kbps AUDIO declares that profile in audio metadata; senders MUST NOT change profile within a message.
- The server MUST calculate the checksum while streaming the body to a temporary file.
- The server MUST validate declared size, actual size, checksum, codec limits, and ownership before acknowledgement.
- The server MUST atomically rename the temporary file into the incoming message directory before returning success.
- First acceptance returns `201 Created`.
- Repeating the same sequence, checksum, size, and timing returns `200 OK`.
- Repeating a sequence with different content or timing returns `409 Conflict`.
- An oversized chunk returns `413 Content Too Large` without committing it.

Success response:

```json
{
  "message_id": "server-uuid",
  "sequence": 12,
  "sha256": "…",
  "bytes": 64000,
  "durable": true
}
```

The device MUST retain its local chunk unless it receives a valid response with `durable: true` and matching identity, sequence, checksum, and byte count.

## Upload a sketch chunk

`PUT /v1/messages/{message_id}/sketch/{sequence}`

Sketch chunks use `application/vnd.family.flsk2` and the same checksum and byte-count headers. `X-Chunk-Start-Ms` is encoded in FLSK2 as well and MUST agree with the request header when both are supplied.

A message may omit sketch chunks. Sketch sequences are independent from audio sequences but SHOULD use the audio chunk number covering the same start time. Sparse gaps mean no drawing activity.

The FLSK2 binary contract is defined in [SKETCH-TIMELINE.md](SKETCH-TIMELINE.md).

## Inspect upload status

`GET /v1/messages/{message_id}/upload`

```json
{
  "message_id": "server-uuid",
  "state": "open",
  "audio": {
    "received": [0, 1, 2, 4],
    "bytes": 256000
  },
  "sketch": {
    "received": [0, 2],
    "bytes": 912
  }
}
```

The device SHOULD use this route after reconnect or reboot instead of retransmitting every local chunk. A server MAY return compact ranges when a message contains many chunks.

## Finalize

`POST /v1/messages/{message_id}/complete`

```json
{
  "audio_chunks": 90,
  "sketch_sequences": [0, 2, 7, 12, 18],
  "duration_ms": 180000,
  "source_audio_sha256": "sha256-of-concatenated-chunk-bodies-in-sequence",
  "closed_reason": "button"
}
```

`closed_reason` is one of `button`, `silence`, `duration_limit`, or `recovered`.

`source_audio_sha256` covers the exact accepted chunk bodies concatenated in sequence. For Opus it does not equal the final Ogg file checksum because the server adds container pages. The completed manifest records a separate checksum for `media.ogg` or `media.wav`.

The server MUST:

1. Confirm ownership and `open` state.
2. Confirm every declared audio sequence is present.
3. Validate monotonic start times, duration, codec, aggregate sizes, and checksums.
4. Confirm and validate every declared sketch sequence independently. Gaps in sketch sequence numbers are allowed because sketches are sparse.
5. Assemble or containerize canonical audio without loading the whole message into application RAM.
6. Write the completed manifest and media, flush them, and atomically commit the message directory.
7. Create recipient inbox references.
8. Emit one new-message notification per recipient.

Success returns `201 Created`; an identical repeat returns `200 OK`. Missing chunks return `409 Conflict` with their sequence numbers. Validation failure does not destroy accepted chunks.

## Abort and recovery

- A device MAY request `DELETE /v1/messages/{message_id}/upload` to abandon an incomplete upload. This does not delete a completed message.
- The server SHOULD retain abandoned and inactive incoming directories for at least 24 hours unless disk pressure requires operator intervention.
- The server MUST NOT interpret a disconnected client as an abort.
- A device reboot MUST recover local-only and open messages from its durable outbox.
- A message whose completion acknowledgement was lost is recovered by querying upload state; completion is idempotent.

## Inbox representation

An inbox entry SHOULD contain:

```json
{
  "seq": 42,
  "message_id": "server-uuid",
  "kind": "audio",
  "from": "mazi",
  "from_label": "Mazi",
  "duration_ms": 73420,
  "audio_codec": "opus",
  "has_sketch": true,
  "read": false,
  "position_ms": 0,
  "created_at": "2026-10-04T18:00:00Z"
}
```

Recipient sequence numbers remain useful for carousel ordering. Media routes use the stable message ID.

## Streaming playback

`GET /v1/messages/{message_id}/audio`

- The route MUST serve only completed media.
- It MUST provide `Content-Length`, `Content-Type`, `Accept-Ranges: bytes`, `ETag`, and the message checksum where practical.
- It MUST honor a valid single byte `Range` request and return `206 Partial Content` with `Content-Range`.
- PCM messages SHOULD be served as a canonical WAV file for interoperability.
- Opus messages MUST be finalized as Ogg Opus and served as `audio/ogg`.
- An Opus message MUST expose `GET /v1/messages/{message_id}/index.json`, mapping `time_ms` to an Ogg page `byte_offset`. X02 selects the closest point at or before the desired time, issues a Range request, and decodes sequentially from that page.
- An Opus Ogg range response begins at a page boundary named by the index. Implementations MUST NOT assume an arbitrary byte offset is independently decodable.

The Opus seek index uses this baseline shape:

```json
{
  "media_type": "audio/ogg",
  "file": "media.ogg",
  "duration_ms": 73420,
  "seek_points": [
    {"time_ms": 0, "byte_offset": 0},
    {"time_ms": 2000, "byte_offset": 4382}
  ]
}
```

Seek points MUST be ordered by time and refer to valid Ogg page boundaries. The final point MAY identify the end boundary and is not necessarily independently decodable.
- Authorization is checked against the authenticated user's inbox reference or administrator role.

`GET /v1/messages/{message_id}/sketch`

- Returns the completed FLSK2 timeline or a manifest of sketch chunks.
- The audio playhead is the authoritative rendering clock.

Playback buffering, seek alignment, and UI behavior are described in [STREAMING-PLAYBACK.md](STREAMING-PLAYBACK.md).

## Playhead and read state

- A message becomes read after samples are successfully handed to the speaker, not merely after opening the media response.
- X02 SHOULD update position every 2–5 seconds, on pause, on navigation away, and at end.
- Position updates MUST be monotonic except for an explicit restart or user seek.
- Server position is per recipient, not part of the immutable message directory.

## Administrator deletion and culling

Administrator routes are detailed in [SERVER-MESSAGE-STORAGE.md](SERVER-MESSAGE-STORAGE.md). Protocol requirements:

- A cull operation MUST support preview/dry-run before mutation.
- Deletion MUST remove inbox references before media becomes unavailable.
- Shared media MUST remain while any inbox or protected system reference exists.
- Initial deletion SHOULD move a message directory to trash atomically.
- Permanent purge MUST be a separate explicit action and SHOULD leave an audit record without message content.
- Ordinary TTL filtering MUST NOT delete canonical media.

## Error model

| Status | Meaning |
|---:|---|
| `400` | Malformed metadata, headers, sequence, or media format |
| `401` | Missing or invalid endpoint authentication |
| `403` | Authenticated endpoint/user cannot access this message |
| `404` | Unknown message or media not visible to this user |
| `409` | Idempotency conflict, missing chunks, or invalid state transition |
| `413` | Chunk or aggregate limit exceeded |
| `422` | Media validation failed |
| `507` | Server lacks safe free disk for durable acceptance |

The device treats timeout, disconnect, `5xx`, and an invalid acknowledgement as ambiguous failure and retains the local chunk.

## Server resource rules

- Request size limits MUST be enforced before and during streaming writes.
- The server MUST reserve a configurable free-disk floor and return `507` before knowingly crossing it.
- Concurrent incomplete messages and concurrent chunk writes MUST be bounded per device.
- Paths MUST be generated from validated server-side IDs; client filenames MUST NOT become paths.
- Checksums MUST be verified before atomic commit.
- Completed media responses MUST be streamed from disk rather than materialized as one Python `bytes` object.

## Compatibility

The current single-POST `kind=audio` route may remain temporarily as a legacy adapter. It SHOULD write into the same canonical message directory and apply the same final limits. New X02 long-message work targets `family-message/1`.

Protocol extensions add optional manifest fields or negotiate a new protocol identifier. Implementations MUST ignore unknown optional response fields and MUST reject unknown required codecs or sketch formats.
