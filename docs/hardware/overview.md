# Hardware overview

**See also:** detailed [`HARDWARE.md`](HARDWARE.md), [`BUY.md`](BUY.md).

## Product unit

| Label | Source | Detail |
|---|---|---|
| **Specification** | Espressif / vendor docs | **ESP32-S3-BOX-3** main unit; recommended desk SKU **BOX-3B** (main + **DOCK**, no SENSOR brick required for v1 desk use). |
| **Observed** | Project desk inventory (2026-10) | Multiple BOX-3 units (Mazi, Lynn, Audrey) with stable USB serials; Lynn qual unit uses **ota_0** layout at `0x20000` (not legacy `factory` @ `0x10000`). |
| **Inferred** | — | Arlo (`box-b`) is a separate office unit, not the Lynn storage-qual box. |

## SoC and module

| Item | Label | Value |
|---|---|---|
| Module | **Specification** | **ESP32-S3-WROOM-1** variant **N16R16** — **16 MiB** quad SPI flash + **16 MiB** octal PSRAM. |
| CPU | **Specification** | Dual-core Xtensa LX7, up to **240 MHz**. |
| On-chip SRAM | **Specification** | **512 KiB**. |
| Radio | **Specification** | **2.4 GHz** Wi‑Fi 802.11 b/g/n + Bluetooth 5 LE. **No 5 GHz, no cellular.** |

## Display and touch

| Item | Label | Value |
|---|---|---|
| Panel | **Specification** | **2.4″** **ILI9341** SPI LCD, **320×240**. |
| Touch | **Specification** | Capacitive **GT911**; extra **red circle** touch zone under the glass (customizable). |

## Audio (onboard)

| Item | Label | Value |
|---|---|---|
| Mics | **Specification** | Dual digital mics → **ES7210** ADC (far-field array per Espressif design). |
| Speaker | **Specification** | **8 Ω / 1 W** cone → **ES8311** DAC + **NS4150** class-D (**PA enable GPIO46**). |
| Hardware mute | **Specification** | Top **mute latch** → **GPIO1** (`BSP_MUTE_STATUS`); analog-mutes onboard mic pair when latched down. |

Detail: [`audio.md`](audio.md).

## Buttons and expansion

| Control | Label | Role |
|---|---|---|
| **Mute** (top) | **Specification** + **Inferred** | Stock firmware toggles wake-word; **product must remap to hold-to-talk (PTT)**. |
| **Boot / function**, **Reset** | **Specification** | Flash/recovery; Boot+Reset for download mode. |
| **Gold-finger** | **Specification** | One accessory at a time: **DOCK** or **SENSOR** (not both without custom adapter). |
| **DOCK** | **Specification** | Stand; **USB-C = 5 V in only**; **USB-A = USB 1.1 FS host** (camera, MSC); **Pmod** ×2 (**16 GPIO @ 3.3 V**). |
| **SENSOR** (full kit) | **Specification** | microSD (**SDMMC**), radar/IR, etc.; **replaces DOCK** on the connector. |

## Power and USB (operator)

| Path | Label | Rule |
|---|---|---|
| Flash / debug | **Observed** (AGENTS) | **Main unit USB-C** — not dock USB-C. Unplug dock **USB-A** camera while flashing. |
| Desk power | **Specification** | USB wall power; appliance model. |

## Sensors and camera

| Capability | Label | Status |
|---|---|---|
| IMU, temp/humidity | **Specification** | On-board per Espressif mapping; not v1 product focus. |
| Onboard camera | **Specification** | **None.** Child→parent photos need **UVC/MJPEG** on dock USB-A ([`UVC-CAMERA.md`](UVC-CAMERA.md)). |

## Firmware environment (as-built vs planned)

| Item | Label | Value |
|---|---|---|
| Stack | **Specification** + **Observed** | **ESP-IDF** (pinned **v5.4.2** in qual scripts); **LVGL** UI; codecs on **I2S0**. |
| Product shell | **Observed** | **`make x02`** → `firmware/v1/` (successor to x01 demos). |
| Default partition | **Observed** | **`SINGLE_APP_LARGE`** — factory app **~1.5 MiB** @ `0x10000` on many units. |
| Unpartitioned flash tail | **Measured** (2026-10-04) | **15,175,680 B** after current factory end — not a filesystem until custom `partitions.csv`. |
| X02 + Opus in 1.5 MiB slot | **Measured** | Probe **1,665,808 B** → **overflows by 129,808 B** ([`evidence/x02-opus-partition/`](../evidence/x02-opus-partition/)). |
| Candidate layouts | **Inferred** (tool-validated, not device-selected) | **2.125 MiB** app slot(s) + on-chip FAT outbox — single-factory or dual-OTA; **production choice open**. |
| Lynn BOX backup | **Observed** (H38 evidence) | **`ota_0` @ `0x20000`** — qual restore checks use `qual_app_partition`. |

Stock Espressif firmware is a **wake-word demo**; treat kits as blank HMI before deployment ([`HARDWARE.md`](HARDWARE.md)).
