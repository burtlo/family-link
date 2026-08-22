# Where to buy (one unit to try)

You only need **one box** to try this. Your iPhone, Mac, or PC is the other client. That is the real product shape, not a compromise.

Prefer a kit that includes the **DOCK** (stand + USB-A). Both SKUs below do.

## Order this (US, 2026-08-17 snapshot)

Stock moves. Open the link and check qty before you rely on it.

| What | Link | Last seen | Notes |
|---|---|---|---|
| **ESP32-S3-BOX-3** (full kit) | [Digi-Key 1965-ESP32-S3-BOX-3-ND](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-BOX-3/21556209) | **$49**, ~119 in stock | Easiest US buy. Includes dock **and** extra bricks you can ignore. |
| **ESP32-S3-BOX-3B** (unit + dock only) | [Digi-Key ESP32-S3-BOX-3B](https://www.digikey.com/en/products/detail/espressif-systems/ESP32-S3-BOX-3B/22286690) | ~$44 | Same main unit + dock. No sensor/bracket. Fine if in stock. |
| BOX-3B | [Mouser 356-ESP32-S3-BOX-3B](https://www.mouser.com/ProductDetail/Espressif-Systems/ESP32-S3-BOX-3B) | ~$47 | Authorized. Check qty. |
| BOX-3 full | [Mouser ESP32-S3-BOX-3](https://www.mouser.com/c/?q=ESP32-S3-BOX-3) | varies | Same family. |
| BOX-3 / 3B | [Adafruit #5835](https://www.adafruit.com/product/5835) / [#5883](https://www.adafruit.com/product/5883) | $49.95 / $47.50 | Often **out of stock**. |
| BOX-3B | [The Pi Hut](https://thepihut.com/products/espressif-esp32-s3-box-3b) | £57 | UK/EU. |
| Either SKU | Espressif lists [AliExpress / Amazon US](https://www.espressif.com/en/products/devkits/esp32-s3-box) | ~$49 | Use only if the listing is **Espressif** and photos show the dock. Avoid no-name “BOX Lite” clones. |

**Recommendation for a first try:** Digi-Key **ESP32-S3-BOX-3** at $49 if it still shows stock. You will not use the sensor brick. You will use the main unit, dock, and USB-C cable.

## Also grab

- The kit USB-C cable is usually data-capable. If flashing fails, use a known **data** cable, not a charge-only brick cable.
- A 2.4 GHz SSID (you already have one). If the house splits bands, write down the **2.4** name, not the 5 GHz one.
- Optional later: a cheap **UVC / MJPEG USB webcam** that speaks USB 1.1 full-speed, plugged into the **dock USB-A** (not required to try audio, text, or photos *from your phone*).

## Docs that ship with it

- [User guide (PDF)](https://cdn-shop.adafruit.com/product-files/5835/P5835+ESP32-S3-BOX-3_AIoT_Development_Kit_User_Guide.pdf)
- [Getting started](https://documentation.espressif.com/esp-box/master/docs/getting_started.md)
- [espressif/esp-box](https://github.com/espressif/esp-box) — examples, 3D shell, schematics
- [ESP Launchpad](https://espressif.github.io/esp-launchpad/?flashConfigURL=https://raw.githubusercontent.com/espressif/esp-box/master/launch.toml) — one-click factory firmware (only to prove the hardware; we replace it)

Night-one hardware check: plug USB-C into the **box** (not only the dock), confirm screen, speaker, mics (peel the screen protector), join 2.4 GHz Wi-Fi with the stock UI. Then we flash ours.
