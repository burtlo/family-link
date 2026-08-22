# Before the box arrives

Today is **Monday 17 August 2026**. Digi-Key’s same-day cutoff is **8:00 PM Central**. Two-day air is **two business days**, end of day, not a morning guarantee. Checkout shows the date they will actually promise.

## Expected delivery

| If you order | Ships | 2-day air lands (typical) |
|---|---|---|
| Tonight **before 8:00 PM CT** | Mon 17 Aug | **Wed 19 Aug**, end of day |
| Tonight **after 8:00 PM CT** | Tue 18 Aug | **Thu 20 Aug**, end of day |
| Tuesday before cutoff | Tue 18 Aug | Thu 20 Aug |

A $49 kit does **not** hit Digi-Key’s free 2-day threshold (~$100). You pay for 2-day (~$13 class) or take Ground (often 1–3 days from Minnesota; sometimes as fast, not promised).

Buffer a day for weather or a missed truck. Practical window: **Wednesday–Friday this week** if you order tonight.

## What we can finish without hardware

Most of the product is not the ESP32. The parent client and the server can be real now. The box can be a **twin** that speaks the same API.

```
[box twin in a browser]  --HTTPS-->  [server on your Mac]  <--  [iPhone / Mac parent page]
[later: real BOX-3]
```

### Deliver now (highest value)

1. **Server** — device identity, pairing token, inbox (text / photo / audio file), “new” count, hangout session start/stop. Auth that you can revoke.
2. **Parent web app** — works on iPhone Safari and Mac/PC. Send text, pick a photo (auto-downscale to ~320×240 JPEG), record a clip, start/end PTT hangout, see inbound clips.
3. **Box twin (browser)** — 320×240 viewport. Locked idle with a count. PIN pad. Inbox of text/photo/audio. Hold-to-talk button. Same REST/WebSocket contract the firmware will use.
4. **API contract** — one OpenAPI or markdown spec so firmware is not inventing URLs on day one.

That loop is the product. You can send yourself a photo and a voicemail this week.

### Firmware we can write, not flash

5. **ESP-IDF app** using [`espressif/esp-box-3` BSP](https://components.espressif.com/components/espressif/esp-box-3) so pins/codecs are not hand-wired. Screens: locked / PIN / inbox / recording / hangout. HTTP client against the same API.
6. **Wokwi** — official `board-esp32-s3-box-3` part (16 MB flash/PSRAM). Good for **display, touch, buttons, Wi-Fi to a mock host**. Audio codecs are not a real speaker/mic; do not trust Wokwi for voicemail quality.
7. **QEMU (ESP-IDF fork)** — `idf.py qemu --graphics` for CI and LVGL layout. Virtual framebuffer, not ILI9341/GT911/ES8311. Use for logic tests, not “does the mute key feel right.”

### Wait for the parcel

- ES7210 / ES8311 record and playback quality
- Mute-button PTT next to a Switch
- 1 W speaker vs TV
- USB-A camera on the dock
- 2.4 GHz join at the other house

When it arrives: flash Espressif’s `display_audio_photo` BSP example first (proves screen, touch, speaker). Then flash ours.

## Emulator stack (what to actually use)

| Layer | Tool | Use it for | Do not use it for |
|---|---|---|---|
| Product UX + API | Browser box twin + parent page | Almost everything you will feel | Codec hiss, button size |
| Firmware UI / Wi-Fi | **Wokwi** `board-esp32-s3-box-3` + ESP-BSP | LVGL screens, PIN, HTTP | Real audio, dock USB camera |
| Firmware logic / CI | **ESP-IDF QEMU** | Automated tests, graphics smoke | Touch, I2S, mute key |
| Hardware | The kit | Audio, PTT, desk | — |

Wokwi setup (from Espressif’s BSP writeup): `diagram.json` with `"type": "board-esp32-s3-box-3"`, `wokwi.toml` pointing at the UF2, `idf.py uf2`, then the Wokwi VS Code / Cursor plugin.

Do **not** spend the wait week building a cycle-accurate BOX-3 emulator. Spend it on the server, the iPhone page, and a 320×240 twin.

## Suggested order this week

1. API + server + parent page (text + photo + audio file).
2. Box twin in the browser (PIN, inbox, hold-to-talk → same server).
3. Hangout signaling (Start on phone, twin shows “Dad is here,” PTT both ways in the browser).
4. ESP-IDF skeleton + Wokwi so the first flash is not a blank repo.
5. Hardware day: BSP demo, then our firmware, then iPhone ↔ box on your desk.
