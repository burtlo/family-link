# Device demos — plan

Small ESP-IDF apps on the **ESP32-S3-BOX-3** that each prove one hardware job. The product firmware is a later agent. These demos exist to find out whether this kit can actually do the answering-machine + PTT work before anyone writes a full LVGL app.

Host protocol lives in [`SERVER-DEMOS.md`](SERVER-DEMOS.md). Python twins prove the API without a box. Device demos prove the **board**: codecs, buttons, 320×240, Wi-Fi, and then the same API from ESP-IDF.

One BOX-3 is enough for island demos. Two-box open line (**h20–h22**, Mazi ↔ Arlo), shared drawing (**h26**), and drawing notes (**h27**) need both kits; see [`TWO-BOX.md`](TWO-BOX.md). Until a second kit is on the desk, the other peer for hangout is a Python twin.

## What we are examining

The protocol assumes the box can do things the S3 may or may not do well **at the same time**. Each demo has a mechanical UART pass line and, where it matters, a human listen/look check.

| Risk | Why a demo |
|---|---|
| Stock firmware is a wake-word assistant | Replace it; prove BSP, not Espressif’s demo product |
| ES7210 / ES8311 via BSP | If hold-to-record → speaker is unintelligible, the product is dead |
| Mute key as PTT, not a toggle | Stock firmware uses it for wake-word; we need press/release |
| 1 W speaker next to a Switch | Desk-volume, not a room |
| 320×240 SPI LCD | Short text and a “N new” idle; not a phone UI |
| GT911 PIN pad | Playback lock is a product requirement |
| 2.4 GHz only | Wrong SSID band = no box at their house |
| HTTP + I2S + LCD together | Classic ESP32 contention; live PTT while the screen says “live” |
| Power loss | RAM dies; NVS must keep Wi-Fi + token; playhead stays on the server |
| No WebRTC | PCM over WebSocket, half-duplex, speaker muted while the button is down |

Wokwi `board-esp32-s3-box-3` can stand in for display, touch, and a mock Wi-Fi host. **It cannot stand in for ES7210/ES8311.** Any demo that records or plays runs on the kit.

## Constraints every device demo obeys

- **ESP-BSP `esp-box-3`.** Do not hand-wire ILI9341, GT911, ES7210, ES8311, or the PA on GPIO46.
- Mic is live **only while a button is held**, except **h20–h22**, which use the top **mute latch** as open/away (mics still hardware-dead while the latch is down). No ESP-SR, no wake word.
- Flash through **USB-C on the box**, not the dock. Hold Boot, tap Reset, release Boot if download fails.
- Peel the **screen protector** or the mics are muffled (Espressif).
- Secrets (SSID, password, device token, server URL) live in a gitignored overlay (`sdkconfig.local`, `secrets.h`). Commit `secrets.example.h` only.
- UART 115200. Every demo prints a single grep-able line: `-- PASS h05` or `-- FAIL h05 <reason>`.
- Do not start the product UI until the island and network tracks pass.

## Layout

One IDF project, many demo mains. Twelve copies of `sdkconfig` is how this dies.

```
firmware/
  CMakeLists.txt
  sdkconfig.defaults          # target esp32s3, BSP, no wake word
  sdkconfig.local             # gitignored: SSID, token, http://mac:8080
  secrets.example.h
  common/                     # BSP board init, UART pass helper
  demos/
    h01_bsp_bringup.md        # flash Espressif's example, not our code
    h02_display_count.c
    h03_touch_pin.c
    h04_mute_ptt.c
    h05_loopback.c
    h06_wifi_join.c
    h07_http_me.c
    h08_record_upload.c
    h09_download_play.c
    h10_playhead_reboot.c
    h11_hangout_ptt.c
    h12_live_screen.c
    h13_show_photo.c
    h14_heartbeat_inbox.c
    h15_text_after_pin.c
    h16_https_me.c
    h17_button_panel.c
    h18_playback_screen.c
    h19_message_list.c
    h20_presence.c
    h21_talk.c
    h22_diary.c
    h23_record_idle_stop.c
    h24_device_log.c
    h25_chipmunk.c
    h26_draw.c
    h27_sketch.c
    h28_video.c
```

Select with `-D FAMILY_DEMO=h05` (or equivalent). Shared `common/` is board bring-up only. Demo `.c` files do not call each other.

First flash is **not** from this tree: Espressif BSP example `display_audio_photo` (`espressif/esp-box-3`). That is h01.

## Tracks

Island demos need no server. Network demos need this Mac on the same 2.4 GHz LAN. Protocol demos need the matching server script already green.

```
island (kit only)          network                 protocol (box + twin)
h01 BSP bring-up           h06 Wi-Fi join          h07 GET /v1/me
h02 display + count        (RSSI, DHCP)            h08 record → POST
h03 touch PIN                                      h09 GET blob → play
h04 mute = hold-to-talk                            h10 playhead after reset
h05 mic → speaker loopback                         h11 WS PTT relay
h17 buttons + mute + chirp                         h12 PTT + live screen
h19 inbox list + screen transitions                h13–h15 photo / WS / PIN text
h25 chipmunk voice memo                            h16 HTTPS GET /v1/me
h28 short LCD clip                                 h18 playback screen + stream
                                                   h20–h22 Mazi/Arlo open line
                                                   h26 shared drawing (two kits)
                                                   h27 drawing note (two kits)
```

h01–h06, h17, h19, h25, and h28 can run **in parallel** with server demos 1–4. h07 waits on server demo 1. h16 waits on `scripts/dev_https.py`. h11 waits on server demo 6. h18 waits on the `h18_playback` fixture (`0.0.0.0:8080`). h20–h22 wait on their matching `h20_presence` / `h21_talk` / `h22_diary` hosts and want **two kits** (Mazi = `box-a`, Arlo = `box-b`). h26 waits on `h26_draw` and both kits. h27 waits on `h27_sketch` and both kits.

Default pairing for protocol demos: **this BOX-3 as box-a, Python `twin.py --id box-b` as the peer.** Two kits only after h11 is green against a twin.

## Discrete demos

### h01 — BSP bring-up

**Not our firmware.** Flash Espressif `display_audio_photo`.

- Screen paints, touch responds, speaker makes a sound.
- Serial shows ESP-IDF and the BSP coming up.

**Pass:** the kit is not dead; we used the supported board package. **Fail:** stop and fix cabling / Boot-Reset / wrong USB-C before writing any of ours.

**Reuse later:** proof that `espressif/esp-box-3` matches this SKU.

### h02 — Display and idle count

**App:** `h02_display_count.c`

- Fill 320×240, draw two or three short lines of text.
- Show a large **“2 new”** (locked-idle product).
- Dim the backlight after a few seconds; it is still an LCD, not e-ink.

**Pass:** readable at desk distance. UART `-- PASS h02`.

**Feasibility:** 320×240 is enough for short messages and a count. If you need a paragraph, the product text is too long.

**Reuse later:** LVGL or BSP display init, backlight GPIO.

### h03 — Touch PIN pad

**App:** `h03_touch_pin.c`

- Digit pad 0–9 plus clear. Hard-coded PIN in the gitignored overlay.
- Correct PIN paints “unlocked”; wrong PIN stays locked. No audio, no message body.

**Pass:** taps register without a stylus; red-circle zone does not have to be the pad. UART `-- PASS h03` after one successful unlock.

**Feasibility:** GT911 as the playback lock. Recording-out still does not need a PIN.

**Reuse later:** PIN widget. Timeout/relock can wait for the product app.

### h04 — Mute button is hold-to-talk

**App:** `h04_mute_ptt.c`

- Top **mute** key: GPIO edge, not a software toggle.
- Serial logs `DOWN` on press, `UP` on release, with timestamps. Ignore bounce.
- Screen (optional) shows “held” / “idle.”

**Pass:** press/release, not click-on click-off. UART `-- PASS h04` after one clean down/up pair.

**Human note (not a code fail):** can you hold it without looking? If not, that is the Pmod arcade-button question in [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md), same firmware guts.

**Reuse later:** PTT input used by h05, h08, h11. Red circle can be a second binding later; do not confuse the first demo.

### h05 — Mic → speaker loopback

**App:** `h05_loopback.c` — **must run on the kit.**

- Hold mute: record from ES7210 into PSRAM (cap ~10 s, 16 kHz s16le mono).
- Release: play that buffer on ES8311 (PA on). Mic off the whole time it is not held.
- No Wi-Fi.

**Pass:** spoken sentence is intelligible at ~1 m. UART `-- PASS h05` after a completed play. **Fail if it sounds like noise or a stalled I2S** — do not “fix it in the product app.”

**Feasibility killer.** If this fails, hangout and voicemail fail.

**Reuse later:** record buffer, play buffer, 16 kHz PCM as the on-wire format in [`SERVER-DEMOS.md`](SERVER-DEMOS.md).

### h06 — Wi-Fi join

**App:** `h06_wifi_join.c`

- SSID/password from the gitignored overlay. 2.4 GHz only.
- Print IP, RSSI, channel. Stay associated 60 s.

**Pass:** `-- PASS h06` with an IP. **Fail:** auth reject, or associated to nothing because the SSID is 5 GHz-only.

**Feasibility:** the desk AP and, later, their house AP must be 2.4 GHz. Confirm the name before baking it for a visit.

**Reuse later:** `esp_wifi` init used by every protocol demo. NVS may store the SSID after first join; still never commit it.

### h07 — HTTP auth (box as client)

**App:** `h07_http_me.c`  
**Needs:** server demo 1 running on this Mac; box and Mac on the same LAN.

- `GET /v1/me` with `Authorization: Bearer <token>`.
- Repeat once with a wrong token.

**Pass:** 200 + `device_id` on the good token; 401 on the bad one. `-- PASS h07`.

**Feasibility:** `esp_http_client` to a LAN HTTP server. TLS is **not** this demo — that is h16. Print the Mac’s LAN IP in the overlay, not `localhost`.

**Reuse later:** HTTP client wrapper, bearer header.

### h08 — Record and upload

**App:** `h08_record_upload.c`  
**Needs:** h05 + h07 green; server demo 3.

**Tested 2026-08-23** on the desk kit: clip lands as a playable WAV under `data/03_messages/box-b/` (now `N.wav`).

- **PTT is the red circle** under the LCD (hold to record, release to upload). The top **mute** key is a *latch* wired through logic gates (`BSP_MUTE_STATUS` / GPIO1). While it is down the mics are **hardware-muted** — using it as PTT records silence. Red LED must be off before a take.
- `POST /v1/messages` multipart `kind=audio` + WAV blob (header + PCM).
- Twin or `curl` on the Mac can fetch the blob.

**Pass:** Mac plays the uploaded clip and it is the same take. `-- PASS h08`. A 10 s 16 kHz s16le WAV is ~320 KB; PSRAM is 16 MB — prove we do not OOM.

**Human note:** every take starts with a **hardware click** (button/codec). Trim it on the device (drop the first ~50–100 ms after the ADC opens) or on the server when the WAV is stored. Do not ship that click as part of the voicemail.

**Reuse later:** clip upload path. Chunked POST can wait until a single 10 s POST is proven. Click-trim belongs in that helper, not a one-off in the demo forever.

### h09 — Download and play

**App:** `h09_download_play.c`  
**Needs:** server demo 3; send a fixture WAV from the Mac (twin or curl) to this box’s inbox.

- `GET /v1/messages` then `GET /v1/messages/{id}/blob`.
- Play on the speaker. Do not require a PIN in this demo (h03 already proved the pad).

**Pass:** fixture is intelligible on the 1 W speaker at desk distance. `-- PASS h09`. Human: point the box at the listener, not at the TV.

**Feasibility:** inbound voicemail / “Dad sent a clip.” If it is too quiet, that is a product constraint, not a surprise.

**Reuse later:** blob download + play. JPEG receive can copy this path later with a different decoder.

### h10 — Playhead after power loss

**App:** `h10_playhead_reboot.c`  
**Needs:** server demo 4 semantics.

1. Mac sends three clips (or text) to this box.
2. Box plays 1 and 2, `PUT /v1/playhead {seq:2}`.
3. Tap Reset (or unplug USB-C briefly). Device RAM is gone.
4. On boot: join Wi-Fi from NVS, `GET /v1/me`, default `GET /v1/messages` → only seq 3.
5. Optional: `GET /v1/messages?before=2` plays archive.

**Pass:** `-- PASS h10` after reboot shows unread = 1 (seq 3). Playhead was **not** in device NVS.

**Feasibility:** the answering-machine story on real hardware. NVS **does** keep SSID + token; it must **not** be the source of truth for inbox position.

**Reuse later:** boot sequence of the product app.

### h11 — Hangout PTT through the server

**App:** `h11_hangout_ptt.c`  
**Needs:** server demo 6; `twin.py` as the other peer (or a second box later).

- WebSocket `/v1/ws`, hello, invite/accept as in the server plan.
- Hold mute → `floor_request` → 20 ms PCM frames (640 bytes) while held → `floor_release`.
- Incoming binary frames play on the speaker **only when the button is up**. Button down = speaker muted (half-duplex; no AEC science project).
- Twin writes a capture WAV on the Mac; reverse path: twin sends, box plays.

**Pass:** `-- PASS h11` after one held burst is captured on the Mac and one reverse burst is heard on the box. Print a crude hop time if timestamps exist. No WebRTC.

**Feasibility of the call.** Underruns, multi-second lag, or echo because the speaker stayed up while recording = fail this demo, do not hide it in UI work.

**Reuse later:** WS client, floor + binary frames. Opus can replace PCM later without changing signaling.

### h12 — Live screen while streaming

**App:** `h12_live_screen.c`  
**Needs:** h11 green.

Same as h11, plus the LCD shows a “live” / floor state and refreshes while PCM is moving.

**Pass:** `-- PASS h12` if h11 still holds: no Wi-Fi stall, no periodic audio glitch on every frame paint.

**Feasibility:** SPI LCD + I2S + Wi-Fi on one radio/SoC. If this fails, the product hangout UI must be almost static (paint once, then audio only).

**Reuse later:** what the hangout screen is allowed to do.

### h16 — HTTPS auth (skip-verify LAN)

**App:** `h16_https_me.c`  
**Needs:** `scripts/dev_https.py` (or `01_auth --ssl-certfile`) on the LAN; same tokens as h07. Notes: [`TLS.md`](TLS.md).

Same as h07 (`GET /v1/me` good token then bad) over `https://DEMO_SERVER_HOST:DEMO_SERVER_PORT`. No CA bundle, no SNTP; UART logs skip-verify.

**Pass:** 200 + `device_id`, then 401. `-- PASS h16`. TLS/transport errors: `-- FAIL h16 <reason>`.

**Feasibility:** ESP-IDF HTTPS client to this Mac. Production still needs SNTP + a real CA (or mkcert trusted on the phone). h07 stays HTTP.

**Reuse later:** HTTPS URL + TLS transport; drop skip-verify when time + CA exist.

### h17 — Button panel, chirps, analog mute

**App:** `h17_button_panel.c` — **must run on the kit** (speaker).

**Tested 2026-08-23** on the desk kit: chirps audible; mute GPIO matches the red LED.

- Screen shows live **down / up** for mute (top latch), boot/config, and the red circle.
- Banner shows **microphone muted** or **not muted** from `BSP_MUTE_STATUS` (GPIO1, active-low analog gate). Red LED on the box should match.
- Mute, boot, and LCD tap chirp on press. The **red circle** chirps on **press and release** (higher, then a lower pitch). Mic codec stays closed.

**Pass:** `-- PASS h17` after a chirp and both muted / not-muted GPIO states (toggle the top mute key once). Reset is not shown — it reboots.

**Reuse later:** mute GPIO poll used by h08; button → chirp path is the same I2S port as p06.

### h18 — Audio message playback screen

**App:** `h18_playback_screen.c`  
**Needs:** `python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080` (or `make demo-playback` to smoke the host). This is **not** server 03 — 03 is inbox seq/blob. h18 is a **fixed URL** plus a richer message object. The same host serves a **320×240 web twin** at `/box/` so layout work does not wait on a flash.

```
GET /demo/h18/message?i=N  →  {
  id, sender, sent_at, url, duration_ms, position_ms, read, index, count
}
GET  <url>                 →  16 kHz s16le WAV (stream while playing)
```

- Device `GET`s `i=0` first, paints sender / time / unread / bar starting at `position_ms`.
- **Play** / **Pause** streams `url` into the ES8311 (not a full download-then-play like h09).
- **ROOMVOL** slider: mute + 78…100 by twos (13 notches). Starts muted. Codec **75–100 is audible enough in an active room (fans + cooking)** — that is why the first on-notch is 78. `ROOMVOL_SHOW_LEVEL` paints the codec number for level checks (dev); the product look hides it.
- Codec **100 is in-range** (`esp_codec_dev` maps 100 → 0 dB). Espressif’s BOX-3 BSP example uses 50; this tree’s working playback demos use 50–70. No Espressif doc says 100 is past the speaker. The kit is an **8 Ω / 1 W** cone ([`HARDWARE.md`](HARDWARE.md): desk-volume, not a room). Smooth playback that still breaks up at 100 is **further testing** (90 vs 100, melody vs voice) before capping `ROOMVOL_MAX`.
- **Boot** (GPIO0) loads the next catalog entry and wraps. Message 1 is the generated melody; 2…N are inbox voice clips imported into `demos/server/h18_playback/assets/` (`voice-05` … `voice-22`; `voice-01`–`04` were empty and are skipped). Speaker stream stays open across clips so GPIO46 PA does not drop after clip 1.
- `-- PASS h18` after the JSON is on screen. Tap Play to hear the clip; bar should move.
- **Web twin:** `http://localhost:8080/box/` — same `GET /demo/h18/message?i=N` catalog in a 320×240 LCD plus bezel. **B** / **N** / **→** = Boot (next). **M** = mute latch. Hold **C** or Space = red circle. Mouse click/drag on the glass = touch (Play, timeline, ROOMVOL).

**Pass:** `-- PASS h18` after a 200 + parse + paint. **Fail:** wifi / fetch / JSON.

**Reuse later:** message DTO, playback widgets, HTTP stream into I2S.

### h19 — Message list + transition manager

**App:** `h19_message_list.c` — **kit only** (touch). No server.

Same record shape as h18 (`sender`, `sent_at`, `duration_ms`, `position_ms`, `read`), baked into the binary. Scrollable rows (more than one 320×240 screen). Tap a row → slide to a detail view (same layout language as h18). **Back** slides to the list. Play on detail only nudges the bar — this demo is navigation, not the codec.

**Pass:** `-- PASS h19` after one list→detail and one Back.

**Reuse later:** list widget, `nav_load` push/pop with `lv_screen_load_anim`.

### h20 — Mute as open/away (Mazi ↔ Arlo)

**App:** `h20_presence.c`  
**Needs:** `python -m demos.server.h20_presence.server --host 0.0.0.0 --port 8080` (or `make demo-presence` to smoke the host). Two kits: Mazi = `box-a`, Arlo = `box-b`. Plan: [`plans/mazi-arlo-open-line.md`](plans/mazi-arlo-open-line.md).

The top mute latch is the availability switch, not PTT. Down (red LED on) heartbeats `available: false` (away). Up heartbeats `available: true` (open). POST `/v1/heartbeat` sends our state; the JSON reply includes `self` and `peer` (`name`, `online`, `available`). A change is recorded on that POST; the friend sees it on **their** next heartbeat. Away is not offline — both keep beating.

**Pass:** `-- PASS h20` after a 200 that includes a peer object. Screen: you + friend, open/away. `make demo-presence` proves the relay with two twins.

**Reuse later:** mute GPIO poll, heartbeat body `{available}`, friend presence on the idle screen.

### h21 — Live talk while unmuted

**App:** `h21_talk.c`  
**Needs:** `python -m demos.server.h21_talk.server --host 0.0.0.0 --port 8080`. Same two-kit pairing as h20: `WHO=mazi` / `WHO=arlo` and `PORT=` if both are plugged in.

**Tested 2026-08-24** on both desk kits: Mazi ↔ Arlo communicate through the host (friend mute card + live voice). Same-time two-way with Audrey as tester is the next check.

Start muted. Unmute streams 16 kHz s16le / 20 ms frames on `/v1/ws`. The server copies **both directions** (no invite, no floor). Incoming PCM always plays, so you can hear the friend while you are muted. Each kit also sends JSON `status` (`available`) so the friend card shows **muted** / **talking**, not a stuck waiting line. Speaker volume is the h18 ROOMVOL slider: mute, then 78 … 100 by 2 (starts at 90). Product hangout stays h11 half-duplex PTT; this demo is the conversation experiment. Same-desk echo is expected — use two rooms.

**Pass:** `-- PASS h21` after ~10 frames sent and ~10 received. `make demo-talk` is the host smoke (names + mute relay + duplex PCM).

**Reuse later:** WS copy-through without floor; mute latch as the send gate; ROOMVOL slider.

### h22 — Diary journal while unmuted

**App:** `h22_diary.c`  
**Needs:** `python -m demos.server.h22_diary.server --host 0.0.0.0 --port 8080`.

Unmute opens the mic once for the session and POSTs ~1 s WAV chunks to `/v1/diary` (`session`, `seq`, `blob`). Mute closes the mic and stops POSTs. The server stamps UTC `received_at` and writes under `data/h22_diary/`. Datetime lives on the host — the box has no clock.

**Pass:** `-- PASS h22` after the first 200 upload. `make demo-diary` checks two dated chunks and that the peer diary stays empty.

**Reuse later:** chunked upload, server-owned timestamps, mute as record gate.

### h23 — Toggle record with idle auto-stop

**App:** `h23_record_idle_stop.c`  
**Needs:** h05 + h07 green; server demo 3 (same host as **h08**).

- **Tap the red circle** to start recording; tap again to stop and upload. Unlike **h08**, this is a toggle, not hold-to-record.
- While recording, if no chunk exceeds a speech peak threshold for **30 s**, the take stops automatically and uploads (screen shows `idle stop`).
- `POST /v1/messages` multipart `kind=audio` + WAV blob — same path as **h08**.

**Pass:** `-- PASS h23` after any completed upload (manual stop or idle timeout). Talk once, then stay quiet for 30 s to exercise auto-stop; or tap to stop early.

**Reuse later:** idle timeout for long diary / open-line recordings; threshold tuning for desk vs room noise.

### h24 — Device event log over heartbeat

**App:** `h24_device_log.c`  
**Needs:** `python -m demos.server.h24_device_log.server --host 0.0.0.0 --port 8080`. Two kits optional (peer line like **h20**).

- Same **mute = away / open** heartbeat as **h20**, but each POST may carry a batch of `{seq, ms, lvl, msg}` lines from an in-RAM ring on the box.
- The host appends to `data/h24_device_log/{device_id}.jsonl` and returns `logs_ack` so the box drops acked lines.
- Logs today: boot reason, Wi-Fi join, mute changes, red-circle taps, peer presence changes, heartbeat failures.
- **RAM only** — power loss clears unsent lines. This demo is the upload path to exercise before adding SD/NVS for a persistent outbox ([`STORAGE.md`](STORAGE.md)).

**Pass:** `-- PASS h24` after the first heartbeat 200 with `logs_ack >= 1`. `make demo-device-log` smoke-tests the host. Tail files with `GET /v1/logs` (bearer token).

**Reuse later:** diagnostic log shipping, failed-upload outbox, SD-backed ring + ack cursor in NVS.

### h25 — Chipmunk voice memo

**App:** `h25_chipmunk.c` — **must run on the kit** (mic + speaker). No server.

- **Hold the red circle** to record (cap 8 s, 16 kHz s16le). Release plays the take back immediately.
- Playback is resampled at **5/3** (~+9 semitones): higher pitch and faster, classic chipmunk. The I2S clock stays 16 kHz.
- Drops the first ~80 ms so the button/codec click is not part of the memo.
- Top **mute** latch still hardware-kills the mics — red LED must be off. Same PTT as **h08**, not **h05**.

**Pass:** `-- PASS h25` after one completed chipmunk play. Spoken sentence should be silly but still a few words.

**Reuse later:** local record buffer, click-trim, toy pitch-shift on playback (not a product path).

### h26 — Shared drawing (Mazi ↔ Arlo)

**App:** `h26_draw.c`  
**Needs:** `python -m demos.server.h26_draw.server --host 0.0.0.0 --port 8080`. Two kits: Mazi = `box-a`, Arlo = `box-b`. Same `WHO=` / `PORT=` as **h20**.

Both screens start **black**. Finger down/move/up on the glass sends JSON `{type:stroke, phase, x, y}` on `/v1/ws`. The server copies to the peer; the friend paints the same 320×240 points (white = you, blue = friend). Drag a heart on one kit; it appears on the other. **Boot** sends `clear` so both canvases wipe. No audio.

**Pass:** `-- PASS h26` after the first stroke from the friend is painted. `make demo-draw` is the host smoke (heart polyline both ways + clear).

**Desk success 2026-08-28. High impact** — keep this in the product/friend line; do not treat it as a throwaway glass experiment.

**Reuse later:** live pointer coordinates over the same WS copy-through as h21, RGB565 framebuffer ink (same path as h13 / p12).

### h27 — Drawing note (Mazi ↔ Arlo)

**App:** `h27_sketch.c`  
**Needs:** `python -m demos.server.h27_sketch.server --host 0.0.0.0 --port 8080`. Two kits: Mazi = `box-a`, Arlo = `box-b`. Same `WHO=` / `PORT=` as **h20**.

Not live. Home is a **Draw** button plus **New message** when the inbox has a clip. Draw starts a timed recording (white ink). **Boot** POSTs the stroke list to the friend’s inbox (`/v1/sketches`). The friend can be elsewhere; the note waits. Opening it replays at the recorded speed (blue ink). Boot during playback stops and returns home; the note stays until it plays through. Empty Boot from record returns home without sending.

**Pass:** `-- PASS h27` after a 200 send, or the first inbound point painted. `make demo-sketch` is the host smoke (timed heart Mazi → Arlo, then read).

**Desk success 2026-08-28. High impact** — store-and-forward drawing is a first-class note type, same weight as a voice clip.

**Reuse later:** store-and-forward stroke clips, timed RGB565 replay, home with one outbound control + waiting inbound.

### h28 — Short clip on the LCD

**App:** `h28_video.c` — **kit only** (display). No server.

A ~5 s film at 12 fps: title card **CLIP**, then a bouncing ball over scrolling hills at dusk, then **END**. Painted into a 320×240 RGB565 buffer and blitted full-screen (same path as **h13** / **p12**). Loops. No JPEG decoder, no Wi-Fi, no speaker.

Product v1 is still **not video**. This demo only answers whether the SPI LCD can hold a watchable moving picture.

**Pass:** `-- PASS h28` after the first complete playthrough. UART logs achieved fps. **Fail** if the motion stutters or tears the way LVGL widgets do when they move on this panel.

**Reuse later:** timed RGB565 framebuffer blit. If a parent→child clip ever happens, this is the paint path; download/decode is a different demo.

### x01 — product shell

**App:** `x01_product_shell.c`  
**Needs:** combined host on the LAN (`make demo-combined` / `python -m demos.server.combined.server --host 0.0.0.0 --port 8080`). Parent page: `/app/`.

One state machine: locked (count, no body) → PIN → inbox (text / audio / RGB565 preview) / record-out / hangout PTT. Playhead stays on the server. `-- PASS x01` on first unlock, recording, or hangout `session_start`.

**Reuse later:** this is the previous glue binary (combined host). v1 product is **x02**.

### x02 — v1 product shell

**App:** `x02_product_shell.c`  
**Needs:** `make v1-server` (HTTP `:8080`). Admin: `/app/v1.html`. Box twin: `/box/`.

Hangout roster, per-user PIN, carousel inbox, record 1:1 / Everyone, mute gate, WS `{type:inbox}` refresh. `-- PASS x02` after login. Flash `make x02 WHO=mazi` / `WHO=arlo`.

**Reuse later:** this is the binary to polish for ship.

## Not in this series (on purpose)

- Expressive face / avatar / UI chirps — those are [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md) (p01–p09). Do not fold a pet UI into h02.
- USB-A camera on the dock (no onboard camera; phase later).
- Production CA verify + SNTP (h16 is skip-verify LAN TLS only; see [`TLS.md`](TLS.md)).
- Group mixing (server phase 3).
- Pmod arcade button (only if h04’s human note says the mute key is too small).
- Baking their-house SSID until this-house h06 and h07 are boring.

Parent page lives in `demos/parent/web/` (`/app/v1.html` on the v1 host). Product LVGL app is **x02**.

## How this maps to product firmware

Keep `firmware/common/` and the demo that passed. The product app is a state machine that **calls the same helpers** (PTT gpio, record, play, http, ws). Do not rewrite I2S in a new repo.

Promotion order:

1. Island h01–h05 on the desk kit (replace stock wake-word firmware first).
2. h06–h07 so the box is a client of this Mac.
3. Server demos 1–4 and 6 exist; then h08–h11 against a twin.
4. h12 before any animated hangout UI. Personality p09 (talk vs freeze during playback) is the matching face-budget check.
5. Only then: second kit, other-house SSID, always-on dock.

Personality p01–p07 can overlap island h01–h05 (need screen, PTT, speaker). They must not delay HTTP/hangout demos.

## Decisions this plan makes

- Device demos are **feasibility**, not a miniature product. One job per flash.
- One IDF project, compile-time demo select.
- Other peer is a **Python twin** until h11 is green.
- Playhead is still **server-only**; the box only stores Wi-Fi + token in NVS.
- Live audio is **PCM over WebSocket**, half-duplex, speaker off while transmitting.
- Codecs and concurrent LCD+I2S+Wi-Fi are proven on **hardware**, not Wokwi.
