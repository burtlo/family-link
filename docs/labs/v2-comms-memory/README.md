# V2 product communication-load memory campaign

Last updated: 2026-10-25

Source of truth for each `CASE=` on the proposed `make lab.v2-comms` runner: product firmware, V2 communication contracts, host fixture on port **8777**, and per-case memory measurement tables.

Implementers and agents: [AGENTS.md](AGENTS.md) in this directory.

LAB-16 attached-storage `VARIANT=` scenarios: [lab-16-attached-storage README](../../../client/labs/lab-16-attached-storage/README.md).

## Table of contents

- [Agent and implementer guide](AGENTS.md)
- [Reference — V2 startup capture (not a runnable CASE)](#reference-v2-startup)
- [CASE `signin-idle-baseline` — signed-in idle inbox baseline](#case-signin-idle-baseline)
- [CASE `single-audio-3s` — single short audio-only inbox item](#case-single-audio-3s)
- [CASE `single-audio-sketch-3s` — short audio-plus-sketch item](#case-single-audio-sketch-3s)
- [CASE `long-range-audio-sketch` — long multi-range audio-plus-sketch playback](#case-long-range-audio-sketch)
- [CASE `eight-member-inbox` — eight-item inbox across an eight-person hangout](#case-eight-member-inbox)
- [CASE `inbox-64-mixed` — maximum 64-item mixed inbox](#case-inbox-64-mixed)
- [CASE `inbox-64-all-sketch` — 64-item all-sketch card stress](#case-inbox-64-all-sketch)
- [CASE `inbox-near-limit` — inbox response near the 262,144-byte ceiling](#case-inbox-near-limit)
- [CASE `inbox-response-boundary` — exact inbox response limit boundary](#case-inbox-response-boundary)
- [CASE `inbox-revision-update` — inbox revision add, equal, and stale responses](#case-inbox-revision-update)
- [CASE `playback-inbox-refresh` — playback, media ranges, last-viewed, and inbox refresh](#case-playback-inbox-refresh)
- [CASE `send-audio-only` — outgoing audio-only message](#case-send-audio-only)
- [CASE `send-audio-sketch` — outgoing audio-plus-sketch message](#case-send-audio-sketch)
- [CASE `worker-overlap` — health, session refresh, inbox refresh, and send overlap](#case-worker-overlap)
- [CASE `hangout-roster-0-1-8` — signed-out hangout roster sizes](#case-hangout-roster-0-1-8)

<a id="reference-v2-startup"></a>
## Reference — Captured V2 startup (not a runnable CASE)

**Capture:** The first sample is taken before Wi-Fi startup, SD mounting, microphone initialization, or display/UI construction. The same diagnostic boot then records how heap readings change as storage, microphone, display, workers, and Wi-Fi are brought up. This reference is not a scenario run and cannot replace a same-boot baseline for any CASE below.

**Command used for the captured V2 diagnostic:**

    $env:FAMILY_DEVICE_MEM_DIAGNOSTIC = '1'
    make app KIT=lynn-kit PORT=COM5
    make app:monitor FILTER='product_network:I product_mic:I product_record:I product_mem:I product_send_http:I product_login:I product_login_http:I'

**Capture time and identity:** 2026-10-25, approximately 02:06:15 America/Chicago, from the host timestamp on [runtime-memory.txt](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt). The serial itself has boot-relative time (first full sample at t=19 ms), and this capture has no run manifest or run ID, so the host file time is approximate. Board: lynn-kit on COM5. Build: `80a3f355d76a40e88450691a3577cbb4`. The diagnostic boot markers passed.

**Detailed description:** In app startup, the first full memory sample is logged immediately before the SD mount call. At that point Wi-Fi has not started, the SD card is not mounted, the microphone/I2S has not initialized, and display/UI construction has not started. The later rows are from that same boot as each subsystem starts. This is a real V2 product capture, not a LAB-16 firmware baseline.

**Capture evidence:** The [V2 device memory snapshot](../../product/device-memory-snapshot-2026-10-25.md) explains the capture. Raw samples: [memory-samples.txt](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/memory-samples.txt). Filtered serial: [runtime-memory.txt](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt). Source call order: [app_main.c](../../../client/device/main/app_main.c). This is a startup reference only; it did not run a V2 communication case or LAB-16 variant.

**Important beats:**

- Boot and capture the no-Wi-Fi, SD-unmounted, microphone-uninitialized baseline.
- Mount the SD card and take the next sample.
- Initialize the microphone/I2S and take the next sample.
- Build the display and initial UI, then start the send worker.
- Start Wi-Fi and wait for IP.
- Start the login HTTP worker and take the final boot sample.
- This run did not start recording and did not obtain a successful server response.

**Measurements (KiB):** Hardware capacities are 512 KiB internal SRAM and 16,384 KiB PSRAM. Heap totals/free are allocator readings, not the whole physical chip capacities.

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---:|---:|---:|---:|
| Before SD mount, microphone, display, and Wi-Fi | 245.79 / 145.27 | 237.84 / 137.69 | 62.00 | 16,290.16 / 16,287.67 |
| After SD mount | 245.79 / 144.28 | 237.84 / 136.70 | 62.00 | 16,290.16 / 16,259.10 |
| After microphone/I2S initialization | 245.79 / 137.39 | 237.84 / 129.81 | 62.00 | 16,290.16 / 16,255.97 |
| UI ready | 245.79 / 109.65 | 237.84 / 102.07 | 62.00 | 16,290.16 / 16,255.40 |
| Send HTTP worker ready | 245.79 / 104.48 | 237.84 / 96.91 | 62.00 | 16,290.16 / 16,239.40 |
| Wi-Fi received IP | 245.79 / 35.63 | 237.84 / 28.05 | 27.00 | 16,290.16 / 16,175.49 |
| Login HTTP worker ready | 245.79 / 26.34 | 237.84 / 18.76 | 18.00 | 16,290.16 / 16,159.48 |

At the clean first sample, the 512 KiB physical internal SRAM has 245.79 KiB in the queried internal heap and 145.27 KiB free in that heap. The remaining 266.21 KiB is outside that allocator query; it is not established as free app memory. Of the queried free internal heap, the DMA-qualified query reports 137.69 KiB free, including a largest contiguous block of 62 KiB. The internal-free minus DMA-free query difference is 7.58 KiB not included by the DMA filter. PSRAM reports 16,290.16 KiB in its heap and 16,287.67 KiB free. None of these values is LVGL pool size.


These cases use the V2 product firmware and the V2 communication contracts; they are not LAB-16 diary-server runs. The signed-in product uses three serial HTTP workers: login/health/roster/refresh, inbox/media/last-viewed, and outgoing send. At most one request per worker should be in flight, so up to three exchanges can overlap across workers. Results can remain queued after a network exchange finishes; measure actual response-buffer count/bytes/region rather than inferring memory from queue depth.

The V2 inbox worker has a four-entry job/result queue, a 262,144-byte snapshot response cap, and a 16,384-byte cap for each media response slice. Response storage is requested from PSRAM first, then ordinary malloc. The send worker uploads one 16,384-byte part at a time. The details below link to the V2 contracts and client workers.

**Available commands:** The existing V2 build/flash command is:

    make app KIT=<kit> PORT=<port>

There is no scripted V2 communications case runner yet. Define this command for the cases below:

    make lab.v2-comms CASE=<case-id> KIT=<kit> PORT=<port> SERVER=<HOST_IP>:8777

This is a **proposed command, not an implemented target**. It must resolve the host IP as described above, verify that the V2 fixture server is reachable at `HOST_IP:8777`, run the named fixture, capture the request timeline and device memory probe, and write a run manifest/events/serial record. Do not use or start the LAB-16 server on port 8776 for these cases.
<a id="case-signin-idle-baseline"></a>
## CASE `signin-idle-baseline` — V2 signed-in idle inbox baseline

**Scenario:** Establish the V2 memory state after the endpoint is signed in and its inbox worker has loaded an empty snapshot. Do not play media or send a message.

**Command to define:** CASE=signin-idle-baseline on the proposed V2 runner above. Existing prerequisite build/flash command: make app KIT=<kit> PORT=<port>.

**Run date/time:** NOT RUN. The [2026-10-25 startup capture](../../../logs/device/90185d13cafe450ab3a27ad96bae3014/runtime-memory.txt) stops at startup/login-worker readiness; it did not prove a successful V2 server response or signed-in inbox.

**Detailed description:** Use the desk credential to complete [POST /v2/session/login](../../product/communication-contracts/endpoint-health-and-login.md), then allow [POST /v2/session/refresh](../../product/communication-contracts/endpoint-health-and-login.md) and [GET /v2/inbox](../../product/communication-contracts/inbox-and-media.md) to run. Keep the inbox empty and wait through one 60-second refresh interval. Do not start audio/sketch playback or outgoing sends. This establishes the service and queue baseline for later V2 cases.

Sources: [endpoint health and login contract](../../product/communication-contracts/endpoint-health-and-login.md), [inbox and media contract](../../product/communication-contracts/inbox-and-media.md), [login HTTP worker](../../../client/device/main/product_login_http_worker.cpp), [inbox HTTP worker](../../../client/device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own pre-service baseline before Wi-Fi, SD, microphone, display, or workers add load.
- Join Wi-Fi and capture the IP-ready state.
- Complete PIN login; record request/response sizes and sample after the session is applied.
- Receive and apply the empty valid inbox snapshot; record body size, parse/apply peak, and result-buffer lifetime.
- Keep the empty inbox visible through one ordinary 60-second interval. Record health/session refresh and inbox GET separately, including their actual start/end order, queue/result counts, and buffer bytes/regions.
- Drain results, leave workers idle, and capture settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Wi-Fi, login, and empty inbox ready | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 60-second login refresh peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 60-second inbox poll/apply peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Workers idle after response consumption | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-single-audio-3s"></a>
## CASE `single-audio-3s` — V2 single short audio-only inbox item

**Scenario:** Load one ordinary audio-only card and play a short message. This isolates the smallest normal audio card and media-fetch path.

**Command to define:** CASE=single-audio-3s on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return one inbox item with 3,000 ms of pcm_s16le audio: 96,000 media bytes at 16 kHz, mono, 16-bit. The item has no sketch descriptor. Select and play it to completion through [GET /v2/messages/{message_id}/media/audio](../../product/communication-contracts/inbox-and-media.md). Record actual snapshot JSON bytes, first media prefix, additional Range responses, response-buffer lifetime, and card allocations. The message/audio format is defined by [message delivery](../../product/communication-contracts/message-delivery.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive the one-item [GET /v2/inbox](../../product/communication-contracts/inbox-and-media.md) response; record exact bytes, parse/apply, and card-render peaks.
- Select the item; capture each audio Range request until the contiguous 32,000-byte playback prefix is available, then record playback start.
- Continue fetches through playback completion; record returned bytes, request count, active media-buffer count/bytes/region, and the highest memory sample.
- Stop playback, consume queued results, verify the complete 96,000-byte asset/hash, and measure after the worker and media buffers settle.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Inbox parse/apply and audio prefix | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback/media-fetch peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback complete and idle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-single-audio-sketch-3s"></a>
## CASE `single-audio-sketch-3s` — V2 short audio-plus-sketch item

**Scenario:** Load and play a short item with both audio and a sketch. This measures the selected card's sketch download/replay path in addition to audio.

**Command to define:** CASE=single-audio-sketch-3s on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return one item with 3,000 ms of audio (96,000 PCM bytes) plus an 800-byte flsk2 sketch, representing 100 eight-byte points. Points use valid 0–119 x/y coordinates and nondecreasing t_ms values. Play the item using [GET /v2/messages/{message_id}/media/audio](../../product/communication-contracts/inbox-and-media.md) and render the sketch using [GET /v2/messages/{message_id}/media/sketch](../../product/communication-contracts/inbox-and-media.md). Do not preload media for unrelated cards. See [message delivery and flsk2](../../product/communication-contracts/message-delivery.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive, validate, apply, and render the item with both descriptors; record the snapshot bytes and allocations.
- Select the item and fetch its audio prefix and 800-byte sketch asset; log actual ordering and whether the single inbox worker serialized the requests.
- Start audio after its contiguous prefix is ready; replay all 100 sketch points and capture the active audio/sketch buffer high-water.
- Verify full media byte counts/hashes, stop playback, release/consume results, and measure recovered memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Inbox item applied | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Audio + sketch fetch/replay peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback ended and buffers released | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-long-range-audio-sketch"></a>
## CASE `long-range-audio-sketch` — V2 long multi-range audio-plus-sketch playback

**Scenario:** Play a maximum-duration audio-plus-sketch message whose media must arrive in many bounded Range responses. Keep the selected item active through playback.

**Command to define:** CASE=long-range-audio-sketch on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Use the accepted maximum 180,000 ms audio duration: 5,760,000 bytes of 16 kHz mono signed 16-bit PCM. Attach a deterministic 3,600-point flsk2 sketch (28,800 bytes, 20 points per second over 180 seconds, nondecreasing timestamps). Fetch the assets through [GET /v2/messages/{message_id}/media/audio](../../product/communication-contracts/inbox-and-media.md) and [GET /v2/messages/{message_id}/media/sketch](../../product/communication-contracts/inbox-and-media.md). Playback can start after a contiguous 32,000-byte prefix; each media response is capped at 16,384 bytes by the current inbox worker. A complete audio fetch therefore requires at least 352 responses and the sketch at least 2 if the cap applies to every response; record actual counts. Measure media buffers as they grow and when they are released.

Sources: [accepted audio/sketch limits](../../product/communication-contracts/message-delivery.md), [Range/prefix playback](../../product/communication-contracts/inbox-and-media.md), [inbox worker buffer caps](../../../client/device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive/apply the descriptor-only snapshot; record exact JSON bytes and card allocations.
- Select the message; capture audio and sketch Range headers/lengths, assemble the contiguous 32,000-byte audio prefix, then start playback.
- During all 180 seconds, record each Range request, cumulative/returned bytes, response-buffer lifetime, active media-buffer count/bytes/region, and periodic low-water samples. Do not imply requests overlap on the serial inbox worker.
- Verify all 5,760,000 audio bytes and 28,800 sketch bytes and their hashes; capture the final playback/asset release.
- Drain worker queues and buffers, then capture settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Initial prefix / playback start | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Mid-playback, media buffers at peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback complete and buffers drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-eight-member-inbox"></a>
## CASE `eight-member-inbox` — V2 eight-item inbox across an eight-person hangout

**Scenario:** Load eight messages from members of an eight-person hangout and check card presentation when sender metadata is known, missing, or unrecognized.

**Command to define:** CASE=eight-member-inbox on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return a static eight-item inbox whose senders come from an eight-person hangout; the signed-in endpoint is one of those eight, so there may be at most seven other senders. Include known avatar/background IDs, missing IDs, and unrecognized IDs. Use a mix of audio-only and audio-plus-sketch summaries. Verify initials/default background fallback. Membership and revision mutation belong in CASE `inbox-revision-update`. See [inbox presentation fields](../../product/communication-contracts/inbox-and-media.md) and [endpoint roster polling](../../product/communication-contracts/endpoint-health-and-login.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Sign in and load the static eight-item snapshot; record response bytes, parse/apply peak, and rendered-card count.
- Check known, missing, and unrecognized avatar/background values against the expected initials/background fallbacks.
- Confirm no unrelated audio or sketch media is fetched while rendering or navigating the eight cards.
- Leave the selected card unchanged, drain the inbox worker, and measure settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight-item response parse/apply | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight cards rendered with metadata fallbacks | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Settled inbox | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-inbox-64-mixed"></a>
## CASE `inbox-64-mixed` — V2 maximum 64-item mixed inbox

**Scenario:** Load the maximum 64 supported inbox entries using a mix of audio-only and audio-plus-sketch messages. This measures card and snapshot scaling without loading every media asset.

**Command to define:** CASE=inbox-64-mixed on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return 64 valid items: 32 audio-only and 32 audio-plus-sketch, each with 3,000 ms audio (96,000 bytes) and each sketch descriptor pointing to an 800-byte, 100-point flsk2 asset. The audio and sketch bodies are media routes, not embedded in the snapshot; play only the selected item. Keep the complete JSON snapshot under the 262,144-byte limit with has_more=false and next_cursor=null. Record the actual JSON body size and whether nonselected media is fetched.

Sources: [complete inbox rules and 262,144-byte cap](../../product/communication-contracts/inbox-and-media.md), [media formats](../../product/communication-contracts/message-delivery.md), [inbox carousel client](../../../client/device/main/product_inbox_bridge.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive the 64-item snapshot; sample response receipt, parse, apply, and render.
- Scroll through the carousel without opening media for all entries.
- Play one selected card only and record incremental media memory.
- Return to idle; count retained card images/objects and heap recovery.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 64-item response receipt/parse | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 64 cards applied and rendered | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| One selected item playing | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Returned to idle inbox | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
<a id="case-inbox-64-all-sketch"></a>
## CASE `inbox-64-all-sketch` — V2 64-item all-sketch card stress

**Scenario:** Load 64 messages that all have sketches, then play one selected item. This stresses the card/image path while checking whether sketch playback storage is limited to the active card.

**Command to define:** CASE=inbox-64-all-sketch on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Return 64 valid audio-plus-sketch items. Each has a 3,000 ms audio descriptor (96,000 bytes) and an 800-byte flsk2 sketch (100 points); do not embed media in the inbox JSON and do not fetch all 64 sketches up front. Exercise carousel creation and selecting cards, then load/replay a sketch only for the active item. This distinguishes per-card thumbnail/image allocations from a full sketch playback buffer. The current contract does not define a maximum sketch asset size; this fixture's 800-byte choice is explicit test data, not a product limit.

Sources: [inbox item/media contract](../../product/communication-contracts/inbox-and-media.md), [flsk2 encoding](../../product/communication-contracts/message-delivery.md), [inbox card and sketch ownership](../../../client/device/main/product_inbox_bridge.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Receive, parse, apply, and render all 64 sketch descriptors; record JSON bytes and rendered-card count without fetching sketch assets.
- Navigate the cards without opening media; verify that no inactive sketch is fetched or held as a playback buffer.
- Select one card and load/replay only its 800-byte sketch; record active buffer and draw/replay high-water.
- Switch to another card and verify the old active sketch storage is released or reused; stop and measure retained card plus active-sketch memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 64 cards with sketch descriptors | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Selected sketch download and replay peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Switch/stop and settle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-inbox-near-limit"></a>
## CASE `inbox-near-limit` — V2 inbox response near the 262,144-byte ceiling

**Scenario:** Measure the response-buffer and parser high-water using a valid inbox JSON body close to the maximum accepted response size.

**Command to define:** CASE=inbox-near-limit on the proposed V2 runner above.

**Run date/time:** NOT RUN. Fixture creation is OPEN until the generator can produce a schema-valid near-limit body without adding unsupported padding fields.

**Detailed description:** Target a valid response body of 256,000 bytes (250 KiB), below the accepted 262,144-byte cap. Use only allowed inbox fields and valid media descriptors; record the exact byte count sent and received. Unknown padding fields are not allowed. If the fixture cannot reach the target with valid schema data, stop and report the highest valid size instead of weakening validation. Exercise response allocation, JSON parse, apply, render, and release. Exact-limit and over-limit semantics are covered separately by CASE `inbox-response-boundary` so this fixture has one target size.

Sources: [inbox body cap and validation](../../product/communication-contracts/inbox-and-media.md), [inbox response allocation](../../../client/device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Record the exact content length at the server and client.
- Capture while the response buffer is live and parsing is underway.
- Capture after apply/render and after the response buffer is freed.
- Confirm the 256,000-byte valid snapshot is accepted and applied; record exact item count and hash/size evidence.
- If the schema-valid fixture cannot reach 256,000 bytes, keep the target OPEN and report the largest valid body. Do not pad with unsupported fields or mark the target case PASS at a smaller size.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Response allocation / parse active | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Snapshot applied and rendered | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Buffers released / idle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-inbox-response-boundary"></a>
## CASE `inbox-response-boundary` — V2 exact inbox response limit boundary

**Scenario:** Verify the inclusive 262,144-byte inbox response ceiling and the over-limit rejection behavior against a previously applied valid inbox.

**Command to define:** `CASE=inbox-response-boundary` on the proposed V2 runner above.

**Run date/time:** NOT RUN; no boundary-case run or fixture exists. This case needs a schema-valid generator that can produce both exact-limit and one-byte-over bodies using supported fields only.

**Detailed description:** Start with a known valid cached inbox. Return a complete valid JSON snapshot of exactly 262,144 bytes; the accepted contract allows a body at the ceiling. Then return a schema-valid body of 262,145 bytes. The exact-limit response must apply; the oversized response must be rejected atomically, leave cached membership and selection intact, and enter connecting. Use permitted varying field values or item data to reach the byte counts. Do not add unknown padding fields. If either exact body size cannot be generated without violating the schema, mark fixture construction OPEN and stop.

Source: [accepted inbox byte ceiling and over-limit behavior](../../product/communication-contracts/inbox-and-media.md).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline; apply a small valid cached inbox first.
- Receive the exact 262,144-byte snapshot; record server/client byte counts, response allocation, parse/apply result, selected item, and memory peak.
- Receive the 262,145-byte snapshot; record byte counts, rejection/connecting state, and prove the previous membership and selection remain unchanged.
- Return a valid recovery snapshot and capture its apply/result-buffer lifetime.
- Drain queues and buffers and capture settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Fresh baseline and small cached inbox | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Exact 262,144-byte response parse/apply | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 262,145-byte response rejected | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Valid recovery and buffers drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-inbox-revision-update"></a>
## CASE `inbox-revision-update` — V2 inbox revision add, equal, and stale responses

**Scenario:** Change inbox membership and sender presentation while signed in. Confirm a newer snapshot adds a card, an equal revision can update presentation, and a lower revision cannot replace newer membership.

**Command to define:** CASE=inbox-revision-update on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Start at inbox_revision 100 with eight items. Add one item and return revision 101. Return revision 101 again with a changed sender name/avatar/background for an existing message. Then return stale revision 100. Requests occur through [GET /v2/inbox](../../product/communication-contracts/inbox-and-media.md) on its normal 60-second schedule or an accepted trigger; there is no push channel. Measure response buffers and the rendered card set at each apply.

Sources: [revision and sender presentation rules](../../product/communication-contracts/inbox-and-media.md), [inbox client](../../../client/device/main/product_inbox_bridge.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Apply revision 100 and record the eight-card baseline.
- Apply revision 101 with a ninth card; selection must not move.
- Apply equal revision 101 with sender name/avatar/background changed; membership/order/selection remain, matching-card presentation updates.
- Apply stale revision 100 with a changed sender field; it must not replace membership or update presentation.
- Settle and record heap/queue recovery.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight-item revision 100 | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Revision 101 adds ninth item | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Equal/stale revision handling peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Settled after apply | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-playback-inbox-refresh"></a>
## CASE `playback-inbox-refresh` — V2 playback, media ranges, last-viewed, and inbox refresh

**Scenario:** Play one long, finished audio-plus-sketch item while the inbox's scheduled refresh becomes due. Let the selected item settle so the one-second last-viewed write is also exercised.

**Command to define:** CASE=playback-inbox-refresh on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 fixture run has been captured.

**Detailed description:** Use the 180-second item from CASE `long-range-audio-sketch`: 5,760,000 PCM bytes and its deterministic flsk2 sketch. Start playback from a 32,000-byte contiguous prefix. Continue Range requests through [GET /v2/messages/{message_id}/media/audio](../../product/communication-contracts/inbox-and-media.md) and allow one scheduled 60-second [GET /v2/inbox](../../product/communication-contracts/inbox-and-media.md) while playback continues. Keep selection stable for at least one second to cause one [PUT /v2/inbox/last-viewed](../../product/communication-contracts/inbox-and-media.md). Inbox, media, and last-viewed calls share one serial inbox worker; record due, enqueue, start, and completion times, queue order, and response-buffer lifetime. The test measures scheduling and queueing, not simultaneous HTTP requests on that worker.

Sources: [inbox polling and last-viewed contract](../../product/communication-contracts/inbox-and-media.md), [media formats/limits](../../product/communication-contracts/message-delivery.md), [inbox HTTP worker](../../../client/device/main/product_inbox_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Start 180-second audio-plus-sketch playback and record active media buffers.
- Keep playback active across the next refresh deadline; record the inbox GET's due time, queue delay, actual start/end, and effect on subsequent media Range reads.
- Keep selection still for one second; record the last-viewed PUT's due time and actual serialized position relative to media/inbox work.
- Record worker queue/result occupancy and every outstanding response buffer through the sequence; do not label same-worker jobs as concurrent.
- Finish or stop playback, consume results, release media buffers, then sample settled memory.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback active before refresh | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Refresh/media/last-viewed queue peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Playback stopped, worker drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-send-audio-only"></a>
## CASE `send-audio-only` — V2 outgoing audio-only message

**Scenario:** Record and send a finished audio-only message to two frozen hangout recipients, then return to the inbox. Keep normal inbox refresh enabled and observe any refresh that naturally falls during upload; this case alone does not claim cross-worker overlap.

**Command to define:** CASE=send-audio-only on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 send run has been captured against the V2 server.

**Detailed description:** Send an 8,000 ms pcm_s16le item: 256,000 audio bytes, no sketch, to two frozen recipients. The part size is 16,384 bytes, so the audio uses 16 sequential parts: fifteen full 16,384-byte parts and a final 10,240-byte part, followed by complete. After finish, return to the inbox while the send worker continues. Keep the 60-second inbox refresh and health/session requests enabled. See [message create/part/complete routes](../../product/communication-contracts/message-delivery.md), [session and health routes](../../product/communication-contracts/endpoint-health-and-login.md), and [send HTTP worker](../../../client/device/main/product_send_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Record the 8-second message, freeze two recipient IDs, and finish.
- [POST /v2/outgoing](../../product/communication-contracts/message-delivery.md); upload audio parts sequentially with [PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}](../../product/communication-contracts/message-delivery.md); then [POST /v2/outgoing/{message_id}/complete](../../product/communication-contracts/message-delivery.md).
- Return to inbox and record whether its scheduled GET actually falls while the send worker is active; include due/start/end times and queue/result-buffer lifetime. If upload finishes first, mark only the overlap observation INCOMPLETE; CASE `worker-overlap` owns the guaranteed worker-overlap case.
- Record final receipt/status, recipient set, part count, and memory after the send worker drains; if the complete response is lost, use [GET /v2/outgoing/by-client-id/{client_message_id}](../../product/communication-contracts/message-delivery.md) to resolve status.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Recording/finish and send creation | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Audio part upload; observe inbox/health refresh if due | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Complete accepted and send drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-send-audio-sketch"></a>
## CASE `send-audio-sketch` — V2 outgoing audio-plus-sketch message

**Scenario:** Send one finished audio-plus-sketch item and measure both media-part streams. The sketch contains a deterministic pattern rather than an unbounded drawing. Keep inbox refresh enabled and report an overlap only if a refresh is observed during the upload.

**Command to define:** CASE=send-audio-sketch on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 send run has been captured against the V2 server.

**Detailed description:** Send 8,000 ms of audio (256,000 PCM bytes) plus an 800-byte flsk2 sketch (100 eight-byte points) to two frozen recipients. Audio uses sixteen 16,384-byte-or-smaller parts; the sketch uses one part. Upload starts only after recording has finished. Return to inbox while the send worker completes. Verify the server accepts the recipient set atomically and the sender does not receive a self-copy.

Sources: [message delivery routes and flsk2](../../product/communication-contracts/message-delivery.md), [send implementation](../../../client/device/main/product_send_http_worker.cpp), [outbox builder](../../../shared/message_delivery/fl_product_send/outbox.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Record audio and 100 sketch points; stop and freeze recipients.
- [POST /v2/outgoing](../../product/communication-contracts/message-delivery.md); upload 16 audio parts and one sketch part serially with [PUT /v2/outgoing/{message_id}/parts/{kind}/{sequence}](../../product/communication-contracts/message-delivery.md); then [POST /v2/outgoing/{message_id}/complete](../../product/communication-contracts/message-delivery.md).
- Return to the inbox; record whether a scheduled [GET /v2/inbox](../../product/communication-contracts/inbox-and-media.md) or health/session exchange actually lands during upload, with queue and response-buffer lifetimes.
- Confirm accepted receipt, recipient publication, no self-copy, and memory recovery; if the complete response is lost, use [GET /v2/outgoing/by-client-id/{client_message_id}](../../product/communication-contracts/message-delivery.md) to resolve status.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Recording and sketch capture | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Audio/sketch part upload peak | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Complete accepted and send drained | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-worker-overlap"></a>
## CASE `worker-overlap` — V2 health, session refresh, inbox refresh, and send overlap

**Scenario:** Run the three HTTP workers under their ordinary periodic schedules while one finished message is uploading. This measures cross-worker high-water and queued-result lifetime under the deterministic overlap fixture profile.

**Command to define:** CASE=worker-overlap on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 concurrency run has been captured against the V2 server.

**Detailed description:** Keep an endpoint signed in and displaying its inbox. [POST /v2/endpoint/health](../../product/communication-contracts/endpoint-health-and-login.md) and [POST /v2/session/refresh](../../product/communication-contracts/endpoint-health-and-login.md) share the login worker and serialize there; [GET /v2/inbox](../../product/communication-contracts/inbox-and-media.md) uses the inbox worker. The scenario must produce at least one observed interval with an exchange active on each of the login, inbox, and send workers. Use a deterministic 180-second outbound audio-plus-sketch fixture (5,760,000 audio bytes plus 28,800 sketch bytes) and a versioned fixture-server profile that delays each part response by 1 second, keeping each response within the configured client timeout. This keeps send active across multiple ordinary 60-second deadlines; the induced delay is test input, not a production latency claim. Record every request's due/enqueue/start/end time, actual overlap count, queue/result-buffer lifetime, and response bytes. If the worker overlap is not observed or the delay exceeds a client timeout, mark the scenario INCOMPLETE and preserve the actual sequence; do not claim the intended overlap.

Sources: [health/login worker contract](../../product/communication-contracts/endpoint-health-and-login.md), [inbox/media worker contract](../../product/communication-contracts/inbox-and-media.md), [message delivery contract](../../product/communication-contracts/message-delivery.md), [login HTTP worker](../../../client/device/main/product_login_http_worker.cpp), [inbox worker](../../../client/device/main/product_inbox_http_worker.cpp), [send worker](../../../client/device/main/product_send_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline.
- Sign in and load the inbox; capture a clean ready/idle state and the next due times for login-worker and inbox-worker schedules.
- Start the deterministic 180-second outbound capture and send with the versioned 1-second-per-part fixture profile; record the recording and upload beats separately.
- Observe multiple health, session-refresh, and inbox-refresh deadlines while the send worker is active; record login-worker serialization and inbox-worker queue order.
- Prove at least one interval with one exchange in flight on each of the three workers; record start/end intervals, response buffers, and maximum in-flight exchanges.
- Verify complete/receipt state and that every result buffer is consumed/released; wait for all workers and queues to settle and measure recovery.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| 180-second recording and finished payload queued | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Three-worker overlap during delayed part upload | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Health/session serialization and result queues | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| All workers idle and buffers released | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

<a id="case-hangout-roster-0-1-8"></a>
## CASE `hangout-roster-0-1-8` — V2 signed-out hangout roster sizes

**Scenario:** Measure the signed-out roster path with zero users, one user, and eight users. Verify that each response updates the roster at the next poll without treating it as an inbox push.

**Command to define:** CASE=hangout-roster-0-1-8 on the proposed V2 runner above.

**Run date/time:** NOT RUN; no V2 roster scenario has been captured against the V2 server.

**Detailed description:** Keep the endpoint signed out. Return valid [GET /v2/hangout](../../product/communication-contracts/endpoint-health-and-login.md) responses with 0, then 1, then 8 members on separate poll cycles, followed by a response with one member removed (7 remain). Check that cards are added/removed only after each response applies. Active roster polling is normally every 60 seconds. Do not run this as a signed-in inbox event: V2 uses separate readiness/roster and inbox exchanges. The five-minute poll after ten quiet minutes is a separate cadence observation and is not required for the 0/1/8/removal case.

Sources: [hangout readiness and roster contract](../../product/communication-contracts/endpoint-health-and-login.md), [login HTTP worker](../../../client/device/main/product_login_http_worker.cpp).

**Important beats:**

- Cold boot the exact scenario build and capture its own clean baseline; verify signed-out state and no inbox request.
- Return the empty roster; record response bytes, apply/render peak, and zero-card state.
- Return one member on the next poll; verify one selectable card and capture memory.
- Return eight members; include known and fallback avatar/background metadata, verify all cards, and capture memory.
- Return seven members after removing one; verify the removal only after the valid response applies and capture settled memory.
- Keep the endpoint signed out throughout and confirm no inbox traffic. Record any idle-cadence probe separately from this case.

**Measurements (KiB):**

| Moment | Internal SRAM heap total / free | DMA-capable internal SRAM total / free | Largest free DMA-capable SRAM block | PSRAM heap total / free |
|---|---|---|---|---|
| Same-boot clean baseline | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Empty roster response | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| One-member roster | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Eight-member roster and presentation fallbacks | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Seven-member roster after removal | NOT RUN | NOT RUN | NOT RUN | NOT RUN |
| Roster worker idle | NOT RUN | NOT RUN | NOT RUN | NOT RUN |

## Current findings and what remains to run

- The V2 startup reference capture is measured on the V2 product build: before storage, microphone, display, and Wi-Fi load, it had 145.27 KiB free in the internal SRAM heap, 137.69 KiB free in its DMA-capable internal SRAM subset, a largest free DMA block of 62 KiB, and 16,287.67 KiB free in PSRAM.
- The same V2 boot reached 18.76 KiB free in the DMA-capable internal SRAM query and an 18 KiB largest free block after Wi-Fi and login-worker startup. It never entered Record and never confirmed a response from the intended host-IP endpoint on port 8777.
- All `CASE=` rows in this document are **NOT RUN**. The proposed `make lab.v2-comms` runner, registry/evidence identity, deterministic fixture controls, and per-phase reports still need implementation.
- The exact-limit fixture (`CASE=inbox-response-boundary`) and delayed-part profile for `CASE=worker-overlap` are explicit implementation inputs; they do not yet exist.

Contract links and probe paths: [AGENTS.md](AGENTS.md).
