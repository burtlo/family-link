# ESP32-S3-BOX-3 — specs vs requirements

Collected 2026-08-17 from Espressif’s user guide, [esp-box](https://github.com/espressif/esp-box), Zephyr’s board doc, Adafruit, and Digi-Key. Street prices move; treat them as a class, not a quote.

## Buy this SKU

**ESP32-S3-BOX-3B** (main unit + dock). You do not need the full kit’s sensor brick, bracket, or breadboard adapter.

| SKU | In the box | Why |
|---|---|---|
| **BOX-3B** | Main unit, **DOCK** stand, USB-C cable, RGB LED + jumper wires | Desk stand + USB-A for a later camera. Right kit. |
| BOX-3 (full) | 3B plus SENSOR, BRACKET, BREAD | Radar, IR, 18650 slot, microSD — unused for a plugged-in desk. |

Indicative price: **~$44–50** each (Digi-Key BOX-3B listed $43.75; Adafruit 3B $47.50, often out of stock). Two children ⇒ two kits.

Factory firmware is a wake-word demo (“Hi E.S.P.”). **Replace all of it.** Do not ship Espressif’s always-listening assistant into the other house.

## Main unit

| Item | Spec |
|---|---|
| Module | ESP32-S3-WROOM-1 (**N16R16**: 16 MB Quad flash + 16 MB Octal PSRAM) |
| CPU | Dual-core Xtensa LX7 @ up to 240 MHz, vector/AI instructions |
| SRAM | 512 KB on-chip |
| Radio | **2.4 GHz** Wi-Fi 802.11 b/g/n + Bluetooth 5 LE. **No 5 GHz. No cellular.** |
| Display | 2.4″ **ILI9341** SPI LCD, **320×240** |
| Touch | Capacitive **GT911**; extra “red circle” under the screen is a customizable touch zone |
| Mics | **Dual digital mics**, **ES7210** codec (far-field array in Espressif’s design) |
| Speaker | **8 Ω / 1 W**, **ES8311** codec + amp (PA on GPIO46) |
| IMU | 3-axis gyro + 3-axis accelerometer |
| Extra sensor | AHT30 temp/humidity (I2C) on some mappings |
| USB | Type-C: power, download, debug |
| Expansion | High-density **PCIe / gold-finger** to dock or other accessories |
| Buttons | **Mute** (top), **Boot / function**, **Reset**. Mute in stock firmware toggles wake-word — remap to PTT. |
| Open hardware | Schematic, PCB, **3D-printable shell**, firmware examples (ESP-IDF; also Arduino / PlatformIO / LVGL) |

Remove the **screen protector** or the mics are muffled (Espressif’s own note).

## Dock (BOX-3-DOCK) — get this

The dock is the desk stand. It also carries the only realistic **photo capture** path.

| Item | Spec |
|---|---|
| Mount | Gold fingers; box sits upright |
| USB-C | **5 V power in only** (keep it plugged in) |
| USB-A | Host: **USB camera up to 720p**, USB disk, HID |
| Pmod | Two Digilent-style headers, **16 GPIOs** at 3.3 V (arcade button later, if the mute key is too small during Minecraft) |

## What we need vs what is on the board

| Requirement | On BOX-3? | Notes |
|---|---|---|
| Desk, always powered | Yes | USB-C; dock is a stand |
| 2.4 GHz Wi-Fi, preload SSID | Yes | Must use the 2.4 GHz SSID if the house splits bands |
| Hold-to-talk / record | Yes | Remap **mute** or the red circle; later a Pmod arcade button |
| Dual mics + speaker | Yes | Enough for async clips and half-duplex PTT |
| Show text | Yes | 320×240 is small; short messages only |
| Show **small photos** (you → child) | Yes | JPEG downscaled on the server; RGB565 frame is 153 KB uncompressed |
| Capture photos (child → you) | **Not onboard** | USB camera on the **dock USB-A**, 720p max. Phase this if you do not want a webcam on the desk. |
| PIN pad | Touchscreen digits | No physical keypad. Fine for v1; Pmod keypad optional |
| Locked idle “N new” | Yes | Backlight can dim; it is still a glowing LCD, not e-ink |
| Live PTT hangout | Feasible | Opus/PCM over Wi-Fi; keep **half-duplex**. Full-duplex AEC next to a Switch is a research project. |
| Group hangout later | Server-side | Boxes stay dumb PTT endpoints; mixing is not the ESP32’s job |
| Cellular | No | Not required |
| Nintendo voice app | No | Irrelevant; voice sits beside the game |

## On-device storage

The answering machine **does not keep the inbox on the box.** NVS holds Wi-Fi + token. Playhead and blobs live on the server ([`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) h10).

| Pool | Size | Role today |
|---|---|---|
| **SPI flash** (WROOM-1) | **16 MiB** | Firmware + NVS in **1.5 MiB** factory partition. **~14.4 MiB unallocated** — not a filesystem until a custom partition table. |
| **PSRAM** | **16 MiB** | Working RAM (one ~320 KB WAV, one 153.6 KB preview). Lost on reset. |
| **Server disk** | Yours | Canonical inbox. |

Removable options: **USB stick on dock USB-A** (BOX-3B) or **microSD in SENSOR brick** (full kit only) — not both at once. **What capacity, speed, and format to buy:** [`STORAGE.md`](STORAGE.md).

Rough local cache if you claim **~12 MiB** on-chip FAT later: **~35–40** ten-second PCM clips in an **outbox** — enough for days of Wi-Fi blips, not a long-term archive. Upload failures and size limits: [`STORAGE.md`](STORAGE.md).

## Gaps to plan for

1. **No camera on the box.** Receive-photos in v1 is easy (your phone → server → LCD). Send-photos needs a cheap UVC webcam on the dock, or wait. How far that can go (still vs clip vs live): [`UVC-CAMERA.md`](UVC-CAMERA.md).
2. **Mute button is small** for Minecraft. If they cannot hit it without looking, add an arcade button on Pmod. Same guts.
3. **1 W speaker** is desk-volume, not a room. Point it at the player, not at the TV speakers. Louder speaker, headphones, external mic (same kit / Pmod): [`SPEAKER.md`](SPEAKER.md).
4. **LCD glow.** E-ink would be calmer idle; this hardware will not do that. Dim the backlight when locked.
5. **Stock firmware is a wake-word product.** Treat the kit as a blank HMI. Flash ours before it goes to their desk.
6. **16 MiB flash is mostly empty and unused.** Firmware sits in 1.5 MiB. Local media needs a custom partition or removable storage — buying guide: [`STORAGE.md`](STORAGE.md).

## Software stack (intended)

- Device: **ESP-IDF** (this is an Espressif HMI; Arduino is possible but IDF + LVGL matches the board).
- Codecs already on I2S: ES7210 (in), ES8311 (out).
- Parent: phone browser or a tiny app.
- Server: HTTPS + object store for clips/photos; a short-lived audio session for hangouts.

## Sources

- [ESP32-S3-BOX-3 user guide (PDF)](https://cdn-shop.adafruit.com/product-files/5835/P5835+ESP32-S3-BOX-3_AIoT_Development_Kit_User_Guide.pdf)
- [Getting started / kit contents](https://documentation.espressif.com/esp-box/master/docs/getting_started.md)
- [espressif/esp-box](https://github.com/espressif/esp-box)
- [Zephyr `esp32s3_box3`](https://docs.zephyrproject.org/latest/boards/espressif/esp32s3_box3/doc/index.html)
- Adafruit #5835 (BOX-3), #5883 (BOX-3B); Digi-Key `ESP32-S3-BOX-3B`
