# Device demos — plan

Small ESP-IDF apps on the **ESP32-S3-BOX-3** that each prove one hardware job. The product firmware is a later agent. These demos exist to find out whether this kit can actually do the answering-machine + PTT work before anyone writes a full LVGL app.

Host protocol lives in [`SERVER-DEMOS.md`](SERVER-DEMOS.md). Python twins prove the API without a box. Device demos prove the **board**: codecs, buttons, 320×240, Wi-Fi, and then the same API from ESP-IDF.

One BOX-3 is on USB today. That is enough for every demo except a two-box hangout. Until hangout works, the other peer is a Python twin on this Mac.

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
- Mic is live **only while a button is held**. No ESP-SR, no wake word, not even “for the demo.”
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
    h17_cross_heartbeat.c
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
                                                   h12 PTT + live screen
                                                   h13–h15 photo / WS / PIN text
                                                   h16 HTTPS GET /v1/me
```

h01–h06 can run **in parallel** with server demos 1–4. h07 waits on server demo 1. h16 waits on `scripts/dev_https.py`. h11 waits on server demo 6.

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

- Hold mute, record, release.
- `POST /v1/messages` multipart `kind=audio` + WAV blob (header + PCM).
- Twin or `curl` on the Mac can fetch the blob.

**Pass:** Mac plays the uploaded clip and it is the same take. `-- PASS h08`. A 10 s 16 kHz s16le WAV is ~320 KB; PSRAM is 16 MB — prove we do not OOM.

**Reuse later:** clip upload path. Chunked POST can wait until a single 10 s POST is proven.

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

### h17 — Cross-Wi-Fi heartbeat

**App:** `h17_cross_heartbeat.c`  
**Needs:** `make demo-cross-heartbeat` (starts 02_heartbeat on `0.0.0.0:8080` and flashes this demo). Box SSID/password in `firmware/secrets.h` must be **2.4 GHz**.

The box is only a client: it joins 2.4 GHz and POSTs `/v1/heartbeat` every 4 s. The Mac running the server can sit on another SSID (5 GHz is fine). Same router without client isolation is enough. Isolated networks: `TUNNEL=1` (cloudflared) or `SERVER_HOST=` a Tailscale/public name.

LCD shows beat count + peer line. This Mac also heartbeats as `box-b`, so the box should see `peer_online`. UART `-- PASS h17` after the first HTTP 200. Server logs `-- heartbeat box-a from <box-ip>`.

**Pass:** `-- PASS h17` and a non-loopback client IP on the server. **Fail:** 5 GHz-only SSID (h06), or AP isolation so the box cannot route to the Mac.

**Reuse later:** the production path is the same: box dials a reachable HTTPS host; no inbound ports at their house.

### x01 — product shell

**App:** `x01_product_shell.c`  
**Needs:** combined host on the LAN (`make demo-combined` / `python -m demos.server.combined.server --host 0.0.0.0 --port 8080`). Parent page: `/app/`.

One state machine: locked (count, no body) → PIN → inbox (text / audio / RGB565 preview) / record-out / hangout PTT. Playhead stays on the server. `-- PASS x01` on first unlock, recording, or hangout `session_start`.

**Reuse later:** this is the binary to polish, not a new I2S stack.

## Not in this series (on purpose)

- Expressive face / avatar / UI chirps — those are [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md) (p01–p09). Do not fold a pet UI into h02.
- USB-A camera on the dock (no onboard camera; phase later).
- Production CA verify + SNTP (h16 is skip-verify LAN TLS only; see [`TLS.md`](TLS.md)).
- Group mixing (server phase 3).
- Pmod arcade button (only if h04’s human note says the mute key is too small).
- Baking their-house SSID until this-house h06 and h07 are boring.

Parent page lives in `demos/parent/web/` (combined `/app/`). Product LVGL app is **x01**.

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
