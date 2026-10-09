# H36 source contract — provisional planning

| Field | Value |
|---|---|
| **Doc kind** | `experiment-contract` |
| **Status** | `provisional planning` |
| **Scope** | H36 isolated generated PCM playback only |
| **Related plan** | [`h36-streaming-playback.md`](h36-streaming-playback.md) |
| **Last updated** | 2026-10-05 |

This document freezes a host-side fixture-server proposal and identifies device gates that must be resolved before firmware build. It does not describe current product-server behavior or authorize implementation. **No H36 implementation, server run, device build, or flash may begin until H35 evidence is committed and reviewed and both this source contract and the H36 plan are reviewed and committed.** Product/X02 and H34 remain unchanged.

## Existing v1 contract: source facts and limits

Sources: `demos/server/v1_product/server.py`, `demos/server/_shared/user_mailbox.py`, `firmware/v1/v1_api.c`, `firmware/v1/v1_auth.c`, and `firmware/v1/v1_types.h`.

- `POST /v1/session/login` requires `Authorization: Bearer …`, accepts `{"user_id":"…","pin":"…"}`, and verifies the PIN. Success is the inbox envelope plus `ok`, `name`, and `pin_reset`; it does not issue a separate auth/session token. Invalid endpoint bearer returns 401, unknown login user 404, and wrong PIN 401 with `ok:false`. The PIN gates client-side playback-request initiation. It is not a credential the playback endpoints validate.
- Playback and inbox requests carry both the device bearer and `X-User-Id`. Missing/unknown user header returns 400; invalid bearer returns 401. `GET /v1/inbox` returns `{user_id,last_viewed_seq,unread,messages,profile}`. Message fields include integer `seq`, `kind`, `from`, `from_label`, `system`, `read`, `position_ms`, `created_at`, and `has_sketch`; optional fields include `duration_ms` and `broadcast_id`. Firmware has a 4096-byte JSON buffer, IDs at most 15 characters (`s_pick_id[16]`), and labels at most 23 characters (`from_label[24]`). Keep H36 metadata bounded to one 4096-byte response and these identity/label widths unless a source-backed change is reviewed.
- `GET /v1/messages/{seq}/blob` checks the selected user's inbox membership through `MAILBOX.read_blob`; a message not visible to that user returns 404. The endpoint returns the complete blob as an in-memory `Response`, with no Range/streaming contract. H36 must not treat it as its media path.
- `PUT /v1/messages/{seq}/position` and `/read` require bearer and valid `X-User-Id`, accept `{"position_ms": integer >= 0}`, and return sequence/position/read. Current `UserMailbox` stores read/position in process-memory `UserSession` dictionaries/sets; it does not persist these values across server restart. Those endpoints do not verify that the sequence belongs to that user. H36 must implement its own recipient check, durable checkpoint transaction, ordering, idempotency, and stale-generation fence.

The H36 adapter will preserve the v1 identity semantics above while providing its own bounded fixture-media and durable-playback-context routes. It must not claim a PIN session token or claim that the v1 product server already has range playback or durable positions.

## Proposed isolated server contract

All routes below belong to the H36-only fixture server. The regular login and inbox paths deliberately mirror v1 shapes; all H36-specific routes are under `/h36/v1/`. No new H36 route is added to `demos/server/v1_product/server.py`.

### Authentication, inbox, and metadata

| Route | Request | Success | Authorization/failure |
|---|---|---|---|
| `POST /v1/session/login` | Bearer; JSON `{user_id,pin}` | Existing v1 success envelope: `{ok:true,name,pin_reset,user_id,last_viewed_seq,unread,messages,profile}`; no token | Bad bearer 401; unknown user 404; wrong PIN 401 and `{ok:false,error:"wrong pin"}` |
| `GET /v1/inbox` | Bearer + `X-User-Id` | Bounded v1 envelope with only that user's completed recipient-visible fixture rows | Bad bearer 401; missing/unknown user 400 |
| `GET /h36/v1/messages/{fixture_id}` | Bearer + `X-User-Id` | One fixture metadata object, at most 4096 encoded bytes | Bad bearer 401; missing/unknown user 400; non-recipient or unknown fixture 404 |

Proposed metadata shape (all string fields are bounded; `fixture_id` is 32 lowercase hex characters):

```json
{
  "fixture_id": "0123456789abcdef0123456789abcdef",
  "seq": 1,
  "kind": "audio",
  "from": "sender-id",
  "from_label": "Generated PCM",
  "to_user_id": "recipient-id",
  "read": false,
  "accepted_samples": 0,
  "duration_ms": 600000,
  "sample_rate_hz": 16000,
  "channels": 1,
  "bits_per_sample": 16,
  "wav_data_offset": 68,
  "media_bytes": 19200068,
  "pcm_samples": 9600000,
  "pcm_sha256": "<64 lowercase hex>",
  "media_sha256": "<64 lowercase hex>",
  "etag": "\"<media-sha256>\"",
  "playback_generation": "<32 lowercase hex>",
  "operation_seq": 0
}
```

The example's 68-byte data offset is illustrative, not a required fixture offset. IDs/labels stay within the firmware widths above. `accepted_samples` is an absolute, message-relative PCM frame/sample offset, not cumulative network bytes. Derive display milliseconds as `floor(accepted_samples * 1000 / 16000)`. For PCM, each sample is one 16-bit mono frame. `pcm_samples` must equal duration × 16 samples/ms for the selected fixtures.

### Immutable generated WAV and bounded parser

- Generate fixtures with `sample(i)=2*(((i*73+(i>>8)*19+0x0217)&0x7ff)-1024)`, serialized signed 16-bit little-endian; seed `0x0217`. No human recordings. Sequence is fixed: seq 1 = 180 seconds canonical; seq 2 = 240 seconds canonical; seq 3 = 600 seconds canonical; seq 4 = 600 seconds with a 68-byte header made by inserting a 16-byte zero-filled `JUNK` chunk after `fmt `. Recipients are demo-a for seq 1, 2, and 4; seq 3 is visible to demo-a and demo-b. A known demo-denied user has no inbox row for any fixture.
- For canonical 44-byte files, PCM payloads are 5,760,000 / 7,680,000 / 19,200,000 bytes. The 600-second canonical total is 19,200,044 bytes; the 68-byte-header total is **19,200,068 bytes**. Fixture manifest records fixture ID, recipient, duration, PCM properties, data offset, data byte length, total media length, PCM SHA-256, whole-WAV SHA-256/strong ETag, generator revision, and seed/parameters. IDs and labels must fit v1 caps. Generate and review golden fixture hashes and the independent segment oracle before making fixture/hash/range-output claims. Media is immutable after manifest publication; an ETag change invalidates an active playback context.
- Parse RIFF/WAVE with a **4096-byte maximum header scan and 32-chunk maximum** as proposed bounds. Before build, verify these candidate bounds against the parser implementation and fixture headers. Use overflow-safe chunk offset and size arithmetic; account for RIFF odd-byte padding; require exactly one supported `fmt ` and one `data` chunk before the scan limit. Accept PCM format tag 1, mono, 16 kHz, 16 bits, block alignment 2, byte rate 32000. Reject duplicate/malformed/truncated chunks, an absent/out-of-bound data chunk, oversized fmt data, inconsistent sizes, non-PCM/compressed data, and any audio whose byte count is not frame-aligned. Never fall back to offset 44.

### Audio response and range contract

| Route | Request | Response contract |
|---|---|---|
| `GET /h36/v1/messages/{fixture_id}/audio` | Bearer + `X-User-Id`; optional `Range: bytes=a-b` and `If-Match: "<etag>"` | Full `200` for a non-range request; exact `206` for a valid range |

Authorization and recipient ACL checks happen before returning metadata or opening media. For every successful body, return `Content-Type: audio/wav`, exact `Content-Length`, `Accept-Ranges: bytes`, and strong stable ETag. A `206` must include exact `Content-Range: bytes a-b/total`. A failed HTTP `If-Match` precondition returns 412; never silently fall back to a full `200`. Stale generation/operation, idempotency, and compare-and-swap conflicts return 409. Malformed ranges return 400; unsatisfiable ranges return 416 with `Content-Range: bytes */total`. A missing/culled recipient-visible fixture returns 404. Unauthorized recipient context/position requests return the same 404 as metadata/audio.

Ranges may include the header during the initial parse. Every byte in the `data` region must align to complete two-byte frames relative to the manifest's actual `wav_data_offset`: an audio start `a` satisfies `(a - wav_data_offset) % 2 == 0`, and an inclusive audio end satisfies `(b + 1 - wav_data_offset) % 2 == 0`. Read the immutable file from disk in bounded blocks; never materialize full media in host memory. The server verifies its manifest/file identity at open and the device independently validates status, ETag, total length, requested start, and range response before consuming samples.

### Durable playback context and checkpoint proposals

Proposed routes, always requiring bearer + selected `X-User-Id` and recipient ACL:

```text
POST /h36/v1/messages/{fixture_id}/playback-context
PUT  /h36/v1/messages/{fixture_id}/playback-position
```

`POST playback-context` begins a new server-issued playback generation for start/resume/seek/restart. Bounded JSON fields: `base_generation` (current ID or null on first start), `operation_seq` (monotonic per selected-user/fixture client), `idempotency_id` (32 lowercase hex), `reason` (`start|resume|seek|restart`), `requested_sample` (absolute PCM sample offset), and strong `etag`. Response returns the new `generation`, accepted starting `accepted_samples`, `operation_seq`, media `etag`, and fixture identity. Look up retained idempotency before stale-base CAS: identical retry returns its original generation; retained ID with changed payload returns 409. An expired retry with old base generation returns 409 and never creates another context. Invalid sample, changed ETag, stale generation/sequence returns 409; recipient denial is 404.

`PUT playback-position` fields: `generation`, increasing `operation_seq`, `idempotency_id`, `etag`, absolute `accepted_samples`, and `state` (`playing|paused|stopped|complete`). The device submits only an offset supported by wrapper-counted, complete-frame I2S acceptance. Within one generation, positions are monotonic nondecreasing. A seek backwards or restart-from-end creates a new generation first; it never decreases position in the old generation. Duplicate `(user, fixture, generation, operation_seq, idempotency_id)` with identical body returns the original acknowledgement. Reusing any sequence/id with different content, stale generation, out-of-order operation, changed ETag, or out-of-range sample returns 409 and does not advance state; recipient denial is 404.

Persist one bounded state record per selected-user/fixture, retaining at most the latest 32 context IDs and 128 position operations per endpoint/user/fixture. Older retries return 409 and are never reapplied. Serialize validation, CAS, ordering, idempotency lookup, and persistence under one per-record lock. A successful acknowledgement is sent **only after** writing a same-directory temporary record, fsyncing it, atomically replacing the current record, and fsyncing the parent directory. Startup reloads the latest durably committed canonical record, whether or not its acknowledgement reached the client. A failure after replace but before successful directory fsync is `UNCERTAIN`: fence further updates and return 503/pending until the canonical record is validated and resynced or startup reconciliation completes. Never roll back the replaced record or claim an acknowledged-only bounded replay guarantee. Other failed writes have no success response; device state remains pending/failed with no promised restart replay bound. State transitions are generation- and operation-sequence-fenced so a late old worker cannot overwrite a newer seek, sign-out, or playback. Mark `read=true` in the first persisted update only after a nonzero, frame-aligned prefix was wrapper-counted, including when the call status is an error.

These are H36 server choices; the existing v1 position/read routes are not this persistence contract.

### Private one-shot fault controls

Faults are deterministic and one-shot, configured only through a loopback-only controller route `POST /__h36/admin/v1/faults` authenticated by a private admin bearer key; there is no key-issuance endpoint and no public fault endpoint. The key stays in the private controller configuration and is never logged or returned. Limit each JSON request to 2048 bytes. Each fault record contains only run ID (32 hex), fixture ID, operation enum, request ordinal (1–10000), byte/sample boundary, stall duration (max 10 seconds), and case ID (32 hex); reject unknown fields and arbitrary filesystem paths. Cap active faults at 16 per run. Cover restart; drop, truncate, stall audio; bad range, changed ETag, cull audio; persistence failure before write and after replace; and lose-ack for context/position after durable commit. Return/log a fired correlation ID for each consumed fault. Set a 15-second controller command deadline, require cancellation/cleanup acknowledgement within that deadline, and report timeout as failure.

## Device source gates and proposed sizing

Candidate device values below are **planning choices**, not source-proven memory, timing, or cancellation guarantees:

| Resource | Candidate | Rationale / gate |
|---|---:|---|
| PCM ring | 32,768 bytes | Fixed capacity; confirm allocation domain, largest contiguous block, lock-free/synchronization design, and concurrent heap headroom before build |
| Start threshold | 8,192 bytes / 256 ms | 32,000 PCM bytes/s; verify full frames and chosen underrun threshold |
| App HTTP receive | 4,096 bytes | `esp_http_client_read` length is `int`; verify internal 2048-byte client buffer interactions, short reads, and stack/heap location |
| Speaker request chunk | 1,024 bytes / 32 ms | Request size only; accepted output is independently counted by the wrapper |
| RIFF scan | 4,096 bytes / max 32 chunks | Bounds candidate; reject anything beyond it, never fall back to 44 |
| Coordinator / network / speaker tasks | 8,192 / 6,144 / 4,096 bytes | Proposed stack depths; confirm IDF byte semantics, linker/map, measured high-water, and internal-heap requirements before flash |

### Counted speaker output and DMA ordering

Source facts: `firmware/managed_components/espressif__esp_codec_dev/platform/audio_codec_data_i2s.c` discards `bytes_written` from I2S and can return `ESP_CODEC_DEV_OK` without an I2S call while output is reconfiguring. Pinned ESP-IDF 5.4.2 `components/esp_driver_i2s/i2s_common.c` updates `bytes_written` as bytes are copied to DMA descriptors; channel stop can end the loop with a partial prefix and `ESP_OK`, while queue timeout can return an error after a partial prefix. `i2s_channel_disable` changes channel state to stop the loop, waits for the writer, and resets DMA queue/pointers. The stock 1000 ms is a per-wait timeout and not a whole-call bound.

Required H36 proposal: an H36-only link wrapper around `i2s_channel_write` calls the real driver and captures requested/accepted bytes, status, call index, fixture/generation, and expected sample offset. Compare wrapper calls around each codec write: OK/no call means zero accepted; count/hash only its exact accepted prefix; retry only its unaccepted suffix; reject counts beyond request, mismatched call context, and incomplete frames. A nonzero complete-frame accepted prefix triggers read-mark persistence even if the call status is an error.

Freeze output as 16 kHz mono signed PCM16 in the left slot through ES8311 hardware volume; no software conversion/mixing, microphone activation, or codec/I2S reconfiguration during active playback. The pinned BSP default creates six DMA descriptors × 240 frames, initializes mono 16-bit Philips format with LEFT slot at 22.05 kHz; the codec open to 16 kHz may reconfigure. Verify the actual linked BSP/config and effective final 16 kHz mono left-slot byte mapping before using any wrapper count as an accepted PCM sample claim. Reconfirm actual DMA geometry in the build manifest; do not turn defaults into runtime measurements.

Network reconnect preserves the active output/DMA channel. Clean pause/resume preserves and drains already-queued frames before checkpoint acknowledgement or HTTP close; measure and log DMA queue lead. Cancellation first fences new writes, lets the sole output worker's in-flight wrapper call settle, counts its exact prefix, then applies the frozen owner-only drain/close policy. Checkpoint acknowledgement occurs after that boundary, and no old-generation output may cross it. Never discard accepted/queued frames and claim continuity. Freeze source-derived timing candidates and a measurement procedure pre-build; measure and pass the actual cancellation/cleanup bound at the relevant device stage. A proposed 50 ms per-wait policy is not a 50 ms whole-call or cancellation guarantee.

### Independent sample oracle

The controller computes expected sample segments independently of device parsing, HTTP response assembly, and speaker wrapper counters. For every run, preserve expected fixture PCM hash/sample count; for each accepted-range trace, independently regenerate/hash the exact expected PCM segment for each `(start_sample,count)` range. Full uninterrupted playback must reconcile to the entire PCM fixture. Seek, pause, cancellation, partial writes, retries, and reconnects reconcile each explicit range separately; intentional gaps from seeks are recorded as such and cannot be mistaken for missing or duplicated playback. Accepted prefixes are hashed exactly once; suffix retries must not double-count prior accepted frames.

## Seven dependency-ordered implementation slices

Each slice has its own source/evidence review gate. Host-only validation cannot satisfy device gates.

1. **Contract freeze:** review this document and `h36-streaming-playback.md`; resolve all host schemas, statuses, auth/ACL rules, IDs/field widths, parser and range boundaries, idempotency/generation conflicts, fsync acknowledgement, private fault semantics, candidate buffer/task/timeouts, linker wrapper, DMA/output format, queue-lead/drain/cancel policy, and measurement procedure. Freeze expected source/build configuration and source-derived caps before build. Device-dependent choices remain blocking; contract freeze does not establish them or authorize an H36 build.
2. **Deterministic media:** implement only generated immutable fixture files/manifests plus a separately implemented PCM segment oracle. Host gate verifies exact 180/240/600-second sample counts/hashes, non-44-byte header, byte lengths, data offsets, and strict 4096-byte/32-chunk parser limits.
3. **Host media/auth service:** implement the isolated login/inbox/metadata/audio range routes. Host gate proves v1-compatible login envelope/statuses, PIN client/server responsibility split, bearer/user/recipient checks, bounded file streaming, exact 200/206/headers/ETag, and malformed/unaligned/not-found responses.
4. **Durable playback context:** implement generation creation, ordered/idempotent accepted-sample updates, backward-seek new-generation fence, atomic fsync/replace/directory-sync acknowledgements, and restart recovery. Host gate injects duplicate, out-of-order, conflicting, stale, failed-persist, ETag-change, and server-restart cases.
5. **Private controller/fault oracle:** add one-shot fault controls and independently calculated expected segment hashes. Host gate proves controls are bound/consumed once, ordinary requests cannot set them, controller deadlines clean up, and all fault outputs/ranges reconcile to expected segments.
6. **Isolated device shell/prebuild gate:** after H35 evidence and device-authority gates, implement only H36 demo login/PIN client gate, bearer+selected-user requests, fixture metadata/UI, and controller actions (audio disabled). Before build, verify expected source configuration, source-derived stack/buffer/HTTP/parser bounds, wrapper link binding, and actual BSP/IDF/codec versions. Before flash, inspect actual ELF/map and app-slot headroom. At startup, prove effective output format/slot byte mapping and actual DMA geometry before any counted playback. No runtime headroom or timing claim from source estimates.
7. **Counted streaming/recovery:** after remaining source and build blockers are resolved, add ring, network/output workers, wrapper-counted writes, sample-range playhead, clean pause/drain/resume, range seek, checkpoints, cancellation, and injected recovery. Device gate includes zero/partial/error writes, exact live-drop continuation, queued-frame drain/queue-lead, cancellation acknowledgement, UI responsiveness, memory/task metrics, fixture oracle reconciliation, and complete restore/readback/boot per the H36 experiment plan.

## Evidence limits

- The existing v1 server is only an auth/inbox shape reference; its blob endpoint is whole-response, and its playback positions are in-memory. H36's isolated service proposal must prove its own range, file streaming, ACL, and persistence behavior.
- Source defaults and proposed byte/stack values are not runtime heap headroom, acoustic output, or cancellation measurements.
- Wrapper-counted I2S/DMA prefixes establish only the driver-reported accepted byte ranges after effective format mapping is verified. They do not prove analog sound, audibility, intelligibility, or absence of distortion.
- Host fixture/segment tests do not prove BOX-3 playback. No H36 work advances until reviewed H35 evidence, this contract, and the H36 plan are committed.
