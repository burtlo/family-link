# X02 v1 message size, memory, and timing

This breakdown is scoped to the **X02 firmware** (`make x02`) uploading to the **v1 product server** (`make v1-server`). It covers 1 second through the configured 3-minute recording cap. Audio is uncompressed 16 kHz, mono, signed 16-bit PCM in a WAV container. The firmware can attach an FLSK sketch to an audio message. The v1 server requires `kind=audio` and a WAV `blob`, so **strokes-only is not a supported v1 message**; its row below is the sketch attachment's encoded size only, with no valid standalone upload time.

## On-wire payload size

Audio is 32,000 bytes/second plus a 44-byte WAV header. FLSK costs 8 bytes per point plus an 8-byte header. Sketch size depends on drawing activity; the table uses 10 points/second as a planning case, with 20 points/second as a heavier case in parentheses. X02 stops sketch capture after 30 seconds, so those cases top out at 2.4 KB and 4.8 KB. The format can hold at most 2,048 points, or 16.4 KB.

| Recording length | Audio only | Sketch bytes only* | Audio + sketch |
|---:|---:|---:|---:|
| 1 s | 32.0 KB | 88 B (168 B) | 32.1 KB (32.2 KB) |
| 5 s | 160.0 KB | 408 B (808 B) | 160.5 KB (160.9 KB) |
| 10 s | 320.0 KB | 808 B (1.6 KB) | 320.9 KB (321.7 KB) |
| 30 s | 960.0 KB | 2.4 KB (4.8 KB) | 962.5 KB (964.9 KB) |
| 60 s | 1.92 MB | 2.4 KB (4.8 KB) | 1.922 MB (1.925 MB) |
| 180 s | 5.76 MB | 2.4 KB (4.8 KB) | 5.762 MB (5.765 MB) |

Sizes are decimal (1 KB = 1,000 bytes); totals exclude small multipart/form-data headers and network framing. `*`Sketch-only figures assume 10 points/second (20 points/second in parentheses) during active capture, stopping at 2,048 points or 30 seconds. After 30 seconds, sketch size does not grow even when audio continues. For sketch-only, the values describe serialized attachment bytes, not a valid v1 server message. Actual user drawings may be much sparser. The absolute largest X02 payload allowed by both configured caps is 5,776,436 bytes: a 180-second WAV plus a maximum-size 16,392-byte sketch.

## Capacity analysis

There are four different limits. They do not currently agree.

| Stage | Current limit | Result |
|---|---:|---|
| X02 recording | 180 seconds / 5,760,044-byte WAV | Enforced by `V1_RECORD_MAX_SEC`. X02 allocates this entire buffer even for a one-second recording. |
| X02 upload memory | No explicit byte check; one second through 180 seconds | The WAV remains allocated while X02 creates a second complete multipart body. A maximum audio-plus-sketch message therefore adds about 5.78 MB at send time. It is designed to fit 16 MiB PSRAM, but has little enough margin that the 180-second case needs hardware measurement before it can be called reliable. |
| v1 server receive/store | No configured maximum | The server reads the entire uploaded WAV into a Python `bytes` object and writes it to disk. The practical limits are host RAM, free disk, concurrency, and any proxy in front of Uvicorn. A 5.78 MB X02 maximum is routine for a desktop server. |
| X02 playback | 327,679 usable bytes | This is the strict end-to-end limit. It holds about 10.238 seconds of WAV audio. A larger response is not rejected; X02 stops reading at the buffer boundary and plays the truncated beginning. |

### X02 memory at send time

Known large allocations are the fixed 5.76 MB recording buffer, the 327.7 KB playback buffer, about 186.4 KB of recording/sketch buffers, and a second multipart upload body approximately equal to the message size. Sketch cards can add up to about 566 KB if all 16 inbox slots allocate a card framebuffer. UI, network, TLS, codec, task stacks, and allocator overhead are additional and are not measured here.

| Recorded audio | Message body, approximately | Known large allocations at upload* | Memory assessment |
|---:|---:|---:|---|
| 1 s | 32 KB | 6.31 MB | Comfortable once the fixed recording buffer has allocated successfully. |
| 10 s | 321 KB | 6.60 MB | Comfortable for sending; also within full-playback capacity. |
| 30 s | 965 KB | 7.24 MB | Likely comfortable for sending, but receivers truncate it. |
| 60 s | 1.93 MB | 8.20 MB | Likely fits; upload speed becomes the larger risk. |
| 180 s | up to 5.78 MB | 12.05 MB, or about 12.62 MB with all sketch-card framebuffers | Theoretical fit in 16 MiB PSRAM, with roughly 4.2 MB left before uncounted runtime use. Marginal until verified from X02's largest free PSRAM block during upload. |

`*`These totals count identifiable large buffers, not total firmware consumption. Contiguous allocation matters: the multipart body requires one free block as large as the request. Total free memory alone does not prove that allocation will succeed.

### Upload-time limit

X02 gives the POST 20 seconds. Ignoring connection setup, TLS, HTTP framing, and retransmission, the minimum sustained uplink needed is approximately:

| Audio length | Payload | Minimum for 20-second timeout |
|---:|---:|---:|
| 10 s | 0.32 MB | 0.13 Mbps |
| 30 s | 0.96 MB | 0.39 Mbps |
| 60 s | 1.92 MB | 0.77 Mbps |
| 180 s | 5.76 MB | 2.31 Mbps |

Real required throughput is higher. On a sustained 1 Mbps uplink, the ideal timeout boundary is about 78 seconds; on 5 Mbps it is beyond the 180-second recording cap. A weak Wi-Fi link can therefore fail a message that still fits memory.

### Server capacity

The v1 server does not enforce a request-body or message-size ceiling. Multipart handling may spool the incoming file, but `await blob_item.read()` then materializes the complete WAV in server RAM. Each concurrent upload therefore costs at least roughly one payload-sized allocation in the application, with framework and parser overhead in addition. Playback reads the complete file into RAM again before returning it.

For X02's configured maximum, budget roughly 6 MB of application memory per active upload or download, plus overhead, and 5.78 MB of disk per stored maximum-size message. Broadcast audio is stored once in the shared blob directory rather than copied for every recipient. Direct messages store one file. Expiration hides old messages from inbox results but does not delete their blob files, so disk usage continues to grow.

The server can comfortably handle every message X02 can currently produce on an ordinary desktop with adequate free disk. The absence of a server-side cap is still unsafe operationally because another client can submit a much larger body and force a matching RAM allocation.

### Effective supported size today

- **Fully usable X02-to-X02 message:** at most 327,679 bytes, or about **10.24 seconds** of audio. Ten seconds is the sensible operational ceiling with small protocol margin.
- **Can be recorded, uploaded, and stored but not fully played by X02:** more than about 10.24 seconds through the configured **180-second / 5.78 MB** maximum.
- **Server maximum:** unbounded in application code. Available RAM, disk, concurrency, and upstream HTTP limits decide when it fails.
- **Strokes-only:** unsupported by the v1 endpoint. A sketch attached to audio is small enough that audio determines all practical thresholds.

## Device memory and processing

| Type | Capture/processing | Peak memory implications |
|---|---|---|
| Audio only | Capture takes the message duration in real time. WAV header creation is constant-time; there is no audio encoding stage. | About 32 KB of PCM per second, plus 44 bytes. Upload currently builds a second full multipart request body in RAM, so a 180-second message needs roughly 11.5 MB for audio and the copied request body together, plus HTTP/UI/runtime memory. |
| Sketch attachment only | Each touch event is recorded as it occurs; packing is a single pass over at most 2,048 points. | Serialized payload is at most 16.4 KB. Firmware also reserves point and packed-blob buffers (about 32.8 KB total) and a 320×240 RGB565 drawing framebuffer (~154 KB). This is a component estimate, not a supported standalone v1 message. |
| Audio + strokes | Audio capture is real time; touch points are captured alongside it. At stop, firmware packs the point list and copies audio plus sketch into the multipart request body. | At 180 seconds, approximately 11.6 MB for WAV and multipart body combined, plus sketch buffers, framebuffer, and application memory. This is tight for an embedded device with 16 MiB PSRAM; peak allocation can fail before the theoretical message-duration cap. |

The firmware duration cap is 180 seconds. Captured audio is buffered until stop, then uploaded as one HTTP POST. The implementation has a 20-second HTTP timeout. The current upload path therefore makes three-minute messages a poor practical target: memory and timeout/connectivity risk grow with the whole clip. Chunked upload or an audio codec would reduce peak memory and failure cost, but neither is the format used by this endpoint today.

## Approximate upload time

The table uses ideal transfer time (`payload bits ÷ sustained upload rate`) and ignores connection setup, TLS, HTTP overhead, retransmits, and server processing. Sketch bytes are included in the combined-message time at the 10 points/second planning rate. Real-world times will be longer; a weak or unstable link can time out.

| Length | Type | At 1 Mbps | At 5 Mbps | At 10 Mbps |
|---:|---|---:|---:|---:|
| 1 s | Audio | 0.26 s | 0.05 s | 0.03 s |
|  | Sketch bytes only* | Not supported | Not supported | Not supported |
|  | Together | 0.26 s | 0.05 s | 0.03 s |
| 5 s | Audio | 1.28 s | 0.26 s | 0.13 s |
|  | Sketch bytes only* | Not supported | Not supported | Not supported |
|  | Together | 1.28 s | 0.26 s | 0.13 s |
| 10 s | Audio | 2.56 s | 0.51 s | 0.26 s |
|  | Sketch bytes only* | Not supported | Not supported | Not supported |
|  | Together | 2.57 s | 0.51 s | 0.26 s |
| 30 s | Audio | 7.68 s | 1.54 s | 0.77 s |
|  | Sketch bytes only* | Not supported | Not supported | Not supported |
|  | Together | 7.70 s | 1.54 s | 0.77 s |
| 60 s | Audio | 15.36 s | 3.07 s | 1.54 s |
|  | Sketch bytes only* | Not supported | Not supported | Not supported |
|  | Together | 15.38 s | 3.08 s | 1.54 s |
| 180 s | Audio | 46.08 s | 9.22 s | 4.61 s |
|  | Sketch bytes only* | Not supported | Not supported | Not supported |
|  | Together | 46.10 s | 9.22 s | 4.61 s |

Audio capture/processing time is the recording duration itself. Sketch point capture happens concurrently with drawing; packing/validation requires work proportional to the point count. Sketch-only has no supported v1 upload time because the server rejects requests without the audio kind and WAV blob. Server-side multipart parsing and storing are not benchmarked; the server reads the upload into memory and writes it to disk.

## Source basis and caveats

The values come from `V1_SAMPLE_RATE=16000`, 16-bit mono capture, `V1_RECORD_MAX_SEC=180`, the packed sketch header/point structs (8 bytes each), the 30-second sketch limit, and the X02 multipart upload implementation. See [STORAGE.md](hardware/STORAGE.md), [v1_record.c](../firmware/v1/v1_record.c), [v1_sketch.h](../firmware/common/v1_sketch.h), the [v1 product server](../demos/server/v1_product/server.py), and [Makefile targets](../Makefile).
