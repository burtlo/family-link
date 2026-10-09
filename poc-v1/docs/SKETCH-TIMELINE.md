# Full-message sketch timeline

## Decision

Sketching may continue for the lifetime of an audio message. Audio time is the canonical clock. No sketch data is emitted while the user is not touching the canvas, so ordinary messages remain small.

The current FLSK v1 format uses one 30-second capture and at most 2,048 points. FLSK2 removes the message-duration coupling by using independently retryable chunks with a message-relative start time.

## FLSK2 chunk format

All integer fields are little-endian. A chunk header is 16 bytes:

| Offset | Type | Field |
|---:|---|---|
| 0 | `char[4]` | Magic `FLS2` |
| 4 | `uint8` | Version `2` |
| 5 | `uint8` | Flags; bit 0 means the pen was already down at chunk start |
| 6 | `uint16` | Point count |
| 8 | `uint32` | Sketch chunk sequence |
| 12 | `uint32` | Message-relative `start_ms` |

Each point remains 8 bytes:

| Type | Field |
|---|---|
| `uint16` | Milliseconds after chunk `start_ms` |
| `uint8` | Phase: `0=down`, `1=move`, `2=up` |
| `uint8` | Flags; zero in v2 baseline |
| `uint16` | X coordinate |
| `uint16` | Y coordinate |

Chunk-relative time keeps points compact while the 32-bit start time supports messages far longer than the current three-minute cap. A sketch chunk must span less than 65,536 ms. Implementations SHOULD close sketch chunks on the same two-second cadence as audio chunks when points exist.

## Capture behavior

- Capture begins with a `down` point and ends with `up` when touch ends.
- A chunk boundary during a held stroke does not create a visible break. The next chunk sets header flag bit 0 and begins with the current position. The renderer preserves the prior pen position when adjacent chunks are available; after an isolated seek it treats that first position as the continuation origin.
- No chunk is written for a time interval with no points.
- Point coordinates are clamped to the declared canvas.
- Timestamps are monotonic within a chunk.
- Chunk start times are monotonic within a message.
- Unknown header or point flag bits are rejected until a later format revision assigns them.
- Sketch capture ending or failing does not invalidate otherwise valid audio.

## Size optimization

The first implementation should keep the fixed-width format and reduce redundant capture events before adding compression:

- Ignore move events whose position and elapsed time have not changed materially.
- Keep every `down` and `up` event.
- Preserve corners by retaining a point when direction changes beyond a configured tolerance.
- Use a maximum capture interval so a long slow stroke still advances.
- Target 20–40 meaningful points per second during active drawing rather than recording every touch-controller report.

At 40 points/s for an entire three-minute message, points consume about 57.6 KB plus headers. Typical sparse drawing is substantially smaller. The protocol allows up to 512 KiB per message so continuous drawing remains possible without making ordinary messages expensive.

Delta coordinates, variable-length integers, and general compression are future options. They should be introduced as a new format identifier only after measured data shows fixed-width FLSK2 is material to storage or transfer.

## Durable upload

- Sketch chunks use the same message ID, durable outbox, checksum, retry, and acknowledgement rules as audio chunks.
- Sketch sequence space is independent because quiet intervals produce no sketch chunk.
- The recommended sequence is the audio chunk number containing the sketch start time.
- Failure to upload sketch leaves it pending locally; audio completion should wait for locally declared sketch chunks unless the user explicitly chooses to send audio without the failed sketch.
- Finalization declares the sketch chunk count and aggregate checksum.

## Final server representation

The server may retain chunk files during upload. On completion it SHOULD concatenate validated FLSK2 chunks into `sketch.flsk2` with a small index of chunk offsets and start times, or retain a manifest plus immutable chunks if that makes range access simpler. The choice must remain directly inspectable and rebuildable from the completed manifest.

The completed representation records:

- Canvas width and height
- Format version
- Total points and chunks
- First and last sketch time
- Byte count and SHA-256
- Offset/time index if concatenated

## Playback and seeking

- Audio samples delivered to the speaker define current time.
- The renderer applies all sketch points whose absolute time is at or before the audio playhead.
- Pause freezes both audio and drawing.
- Resume continues both from the same time.
- Restart clears the canvas and begins at zero.

Seeking forward by replaying every prior point is acceptable for small sketches. To keep seeking bounded for dense or future long messages, the server or device MAY create periodic derived canvas keyframes. Keyframes are cache artifacts, not canonical user media, and can be rebuilt from FLSK2.

Suggested keyframe interval is 15–30 seconds of message time when the sketch has changed. A seek loads the closest earlier keyframe and applies subsequent points up to the target.

## Validation

Reject a sketch chunk when:

- Magic or version is unknown.
- Declared point data exceeds actual bytes.
- Point count or byte limit is exceeded.
- Time moves backward within the chunk.
- Coordinates exceed policy after allowing safe clamping rules.
- Sequence or start time conflicts with an already accepted chunk.
- Checksum does not match.

Validation never executes or interprets client-provided text as a path or instruction.

## Compatibility

Existing FLSK v1 attachments remain readable as short single-chunk sketches. A migration adapter may represent a v1 attachment as one FLSK2 chunk at `start_ms=0`. New long-message work writes FLSK2.

## Experiment evidence to collect

- Actual touch-controller event rate and retained rate after thinning
- Bytes per minute for sparse, intermittent, and continuous drawing
- Visual difference between raw and thinned strokes
- CPU cost while recording and uploading audio concurrently
- Reboot recovery with pending audio and sketch chunks
- Seek reconstruction time with and without keyframes
