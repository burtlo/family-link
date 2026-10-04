# Streaming playback on X02

## Decision

X02 will stream completed audio from the server instead of downloading a full message before playback. Continued network availability while listening is an accepted product assumption. The implementation still needs a bounded buffer because Wi-Fi and server delivery are bursty even when connectivity is available.

## Existing evidence

h18 already demonstrates the essential path in [h18_playback_screen.c](../firmware/demos/h18_playback_screen.c): open HTTP, parse the WAV header, read small pieces, and write them to the BOX-3 speaker. X02 instead reads into the fixed `V1_PLAYBACK_BUF_CAP` buffer before playing in [v1_carousel.c](../firmware/v1/v1_carousel.c).

The h18 code is evidence, not the finished product implementation. In particular, seeking currently downloads and discards preceding bytes rather than asking the server for a range.

## Target design

- A playback task owns the HTTP connection and speaker writes.
- A 32–64 KiB ring buffer separates network reads from codec writes.
- Playback begins after 250–500 ms of PCM is buffered.
- The UI receives state and position events; it does not perform network or codec work.
- Pause closes or suspends the response and persists position.
- Resume uses an HTTP byte range aligned to an audio frame.
- Stop, card navigation, sign-out, and privacy mute cancel playback cleanly.
- A message is marked read only after audio samples are handed to the speaker.

For baseline PCM WAV:

```text
pcm_byte_offset = position_ms × 32
file_byte_offset = wav_data_offset + pcm_byte_offset
```

Offsets must align to the two-byte mono sample size. The server's canonical WAV header may be longer than 44 bytes, so X02 must parse the data chunk rather than assume an offset.

## Buffering behavior

Suggested states:

```text
idle → connecting → buffering → playing ↔ paused
                         ↓          ↓
                      retrying ← underrun
                         ↓
                       failed
```

- A short underrun pauses speaker delivery and returns to `buffering` without advancing the playhead.
- A dropped HTTP connection reconnects using the last fully played sample offset.
- Reconnect attempts are bounded and visible as “reconnecting.”
- The user may stop or leave the message during reconnect.
- End of response is success only when the expected content range or duration was consumed.

Although network availability is assumed, malformed responses, server restart, and transient radio stalls must not produce a false completed playhead.

## Seeking and playhead

- The UI position follows audio actually written to the speaker, not bytes downloaded.
- Persist position every 2–5 seconds, on pause, on navigation away, and at end.
- Seeking issues a new range request; do not consume and discard the prefix.
- Restart from end resets to zero before opening the response.
- Sketch playback seeks to the same audio time. See [SKETCH-TIMELINE.md](SKETCH-TIMELINE.md).

## Server requirements

- Completed, immutable media file with known length
- `Accept-Ranges: bytes`
- Correct `206`, `Content-Range`, `Content-Length`, media type, and stable `ETag`
- Authorization before file access
- File streaming without loading the complete message into Python memory
- Clear failure when the message was culled between inbox load and playback

## Opus considerations

PCM permits exact byte-to-time conversion. Opus needs a standard seekable container or an index mapping time to byte/packet positions. The Opus demo must select that representation before X02 product integration.

The playback interface should expose decoded PCM to the speaker regardless of source codec. Codec selection belongs beneath the carousel and playhead UI.

## Tradeoffs accepted

- Listening depends on a reachable server.
- Playback implementation becomes a state machine instead of one download followed by one write loop.
- Resume and seek need correct range handling.
- Opus adds decoder and container complexity if adopted.

These costs are accepted because streaming removes the message-length RAM ceiling and avoids allocating several megabytes merely to listen.

## Experiment boundary

The first experiment should adapt h18 to the v1 authentication and message metadata shape without modifying X02. It should prove:

- A message longer than three minutes plays completely.
- Pause and range-based resume work near the beginning, middle, and end.
- UI remains responsive.
- A forced connection interruption resumes without replaying more than the chosen checkpoint interval.
- Memory remains bounded independent of message duration.
- Sketch clock callbacks can follow the audio playhead even before full FLSK2 rendering is added.
