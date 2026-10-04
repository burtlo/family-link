# Long-message architecture

## Purpose

This document records the product decisions behind messages longer than the current X02 playback buffer. It is the architectural overview. The wire contract is in [MESSAGE-PROTOCOL.md](MESSAGE-PROTOCOL.md); focused implementation notes and experiment plans are linked below.

## Decisions

1. The server filesystem is the canonical message archive.
2. Outbound recordings must survive loss of Wi-Fi, server availability, application restart, and device reboot until the server durably acknowledges them.
3. Recording uses small chunks. A user still sees one message, one duration, one playhead, and one notification.
4. Playback streams from the server through a bounded device buffer. Continued network availability during playback is an accepted product assumption.
5. PCM at 16 kHz, mono, signed 16-bit little-endian is the required baseline codec. Opus may become the preferred codec after the isolation demo meets its acceptance criteria.
6. Sketch capture may continue for the full message. Audio time is the canonical clock.
7. The server may remove old messages only through an explicit administrator action. Normal inbox expiry or filtering must not silently delete media.
8. An inbox entry references one canonical message ID. Broadcast recipients do not receive duplicate media files.
9. Incomplete uploads never appear in an inbox and never trigger a new-message notification.

## Why the pieces belong together

| Concern | Architectural response |
|---|---|
| A good recording must not be lost | Persist each completed chunk in a durable device outbox before depending on upload success. |
| Weak or interrupted upload | Use numbered, checksummed, idempotent chunks and query the server for missing chunks. |
| Device RAM limit | Keep only the active capture chunk and a small playback buffer in RAM. |
| Long playback | Stream completed media from the server and support byte-range resume. |
| Server RAM growth | Stream request bodies to temporary files and stream responses from files. |
| Server disk organization | Store each message in one stable directory with a manifest and canonical media. |
| Smaller storage and faster uploads | Evaluate Opus without changing message lifecycle or retry semantics. |
| Long synchronized drawings | Store sparse sketch chunks on the same message clock as audio. |
| Later cleanup | Provide explicit administrator culling with preview, audit record, and recoverable trash. |

## End-to-end lifecycle

1. X02 asks the server to create an incomplete message and receives a server-generated message ID.
2. If the server cannot be reached, X02 creates a provisional local ID and records normally. The server associates that provisional ID when connectivity returns.
3. X02 records a short audio chunk, closes it, writes it to durable storage, and atomically records it in the local outbox manifest.
4. A background uploader sends pending audio and sketch chunks. Server acknowledgements are durable and idempotent.
5. X02 deletes a local chunk only after the server acknowledges the same message ID, sequence, byte count, and checksum.
6. When recording ends, X02 marks the local message closed and asks the server to finalize it after every chunk is present.
7. The server validates the manifest, creates canonical media, atomically moves the directory from `incoming/` to `messages/`, creates inbox references, and sends one notification.
8. A recipient streams the completed media. Audio playback time drives sketch rendering and server-side playhead updates.
9. An administrator may later cull messages. Culling removes inbox references and moves canonical message directories to recoverable trash before permanent purge.

## Safety invariants

- A successful chunk response means the chunk and its checksum are durable on the server.
- Retrying an identical chunk is safe and returns success.
- Reusing a sequence number with different bytes is an error.
- Local media is not deleted on timeout, disconnect, or ambiguous response.
- A message is complete only after the server validates every declared chunk.
- Completed message directories are immutable except for separately stored administrative or playhead metadata.
- Server restart reconstructs completed messages from manifests on disk.
- Message identity does not depend on recipient sequence numbers or filesystem paths supplied by a client.
- Deleting an inbox reference does not delete shared media while another reference remains.

## Existing evidence

- X02 currently records a full message and creates a second full multipart body: [v1_record.c](../firmware/v1/v1_record.c).
- X02 currently downloads a complete blob into a 320 KiB playback buffer: [v1_carousel.c](../firmware/v1/v1_carousel.c).
- h18 already streams HTTP audio to the BOX-3 speaker using a small working buffer: [h18_playback_screen.c](../firmware/demos/h18_playback_screen.c).
- h22 already records and uploads approximately one-second chunks and writes multipart sections sequentially: [h22_diary.c](../firmware/demos/h22_diary.c) and [h22 server](../demos/server/h22_diary/server.py).
- Current v1 media is separated across direct, shared, and sketch directories while mailbox state is rebuilt in memory: [user_mailbox.py](../demos/server/_shared/user_mailbox.py).
- Current storage and accessory constraints are documented in [STORAGE.md](STORAGE.md) and [HARDWARE.md](HARDWARE.md).

## Focused documents

- Protocol and limits: [MESSAGE-PROTOCOL.md](MESSAGE-PROTOCOL.md)
- Server filesystem and administration: [SERVER-MESSAGE-STORAGE.md](SERVER-MESSAGE-STORAGE.md)
- Playback implementation considerations: [STREAMING-PLAYBACK.md](STREAMING-PLAYBACK.md)
- Full-message sketch timing: [SKETCH-TIMELINE.md](SKETCH-TIMELINE.md)
- Experiment order: [plans/long-message-experiments.md](plans/long-message-experiments.md)
- Durable outbox experiment: [plans/durable-outbox-demo.md](plans/durable-outbox-demo.md)
- Opus experiment: [plans/opus-demo.md](plans/opus-demo.md)

## Deliberately unresolved

- Which attached storage backend is present on the current device. The outbox demo begins by recording the detected hardware and mount path.
- Whether Opus 16 or 24 kbps becomes the preferred codec. The PCM contract works regardless.
- Final retention policy and trash grace period. Administrator culling is required; automatic age deletion is not assumed.
