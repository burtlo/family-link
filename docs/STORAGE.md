# Storage — what to buy, what breaks, what is missing

**Canonical inbox:** voicemails and photos the parent reads live on **the server you run** ([`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) h10). Playhead is server-side too.

**Outbox (pending upload):** when a child records and the POST fails — Wi-Fi blip, server down, timeout — the clip must sit somewhere until retry succeeds. **That path is not built yet.** Today the take is **discarded**. Local storage (on-chip FAT, USB stick, or SENSOR microSD) is for that **outbox**, not for replacing the server archive.

On-chip flash / PSRAM facts: [`HARDWARE.md`](HARDWARE.md). Camera + USB port sharing: [`UVC-CAMERA.md`](UVC-CAMERA.md).

## What happens today if upload fails

Firmware records into **PSRAM**, POSTs once on button release, then **reuses the buffer**. There is **no retry queue**, **no “failed to send” inbox**, and **no write to flash or SD**.

| Demo / app | On failed `POST /v1/messages` | Clip saved? |
|---|---|---|
| **h08** record-upload | Screen: `upload failed`; demo stops | **No** — next take overwrites `s_pcm` |
| **x01** product shell | Returns to locked/inbox; no failure UI | **No** — same ~320 KB `s_buf` |
| **h22** diary | Screen: `upload fail` ~400 ms; keeps recording | **No** — failed **chunk** is dropped; later chunks may succeed |

So a **temporary Wi-Fi outage** during send, a **sleeping Mac** hosting combined, wrong **LAN IP** in `secrets.h`, or **15 s HTTP timeout** on a slow uplink all mean the same thing: **the child’s message is gone** unless they record again.

**Wi-Fi down at boot** is a separate problem: the box may not join (h06), so it cannot fetch inbound mail either — but that does not preserve an outbound take; nothing was persisted locally in the first place.

**After power loss:** PSRAM is empty. Any clip that never reached the server is lost even if upload had been “about to retry.”

This is called out as a gap in [`DEMO-MAP.md`](DEMO-MAP.md) (*upload retry / “sent” feedback*) and [`plans/v1-demo-set.md`](plans/v1-demo-set.md).

## Minutes-long diary messages (product target)

Real use looks like **Marco Polo-length rambles** — a few **minutes**, not ten seconds. [`REQUIREMENTS.md`](REQUIREMENTS.md) treats that as in scope. The **10 s cap is only in demo firmware** (**h05**, **h08**, **x01**) to prove upload once before anyone tuned timeouts or chunking.

Wire format today is **16 kHz s16le mono** ≈ **32 KB/s** (256 kbps).

| Duration | PCM size (one WAV) | Fits in 16 MiB PSRAM as a single buffer? |
|---|---|---|
| 10 s (demo cap) | ~320 KB | Easily |
| 1 min | ~1.9 MB | Yes, with headroom for LVGL / HTTP |
| 3 min | ~5.8 MB | Tight — possible for a dedicated buffer, risky beside preview + hangout |
| 5 min | ~9.6 MB | **No** as one contiguous take with everything else running |
| 10 min | ~19 MB | **No** — must **stream or chunk**, not “record then POST” |

So **minutes are a software/protocol problem**, not a reason to buy a 128 GB card. The kit already has enough RAM for **short** rambles; **long** rambles need a different record path.

### Likely product shape (not built in x01)

**Do not** raise `MAX_SEC` to 300 and POST one giant WAV. One five-minute file (~10 MB) blows the **15 s HTTP timeout** on weak uplink and loses the **whole diary** on one failed POST.

Prefer **upload while recording** in small pieces — **h22** already proves the host side:

| Approach | Child UX | Upload | Lose on one failure |
|---|---|---|---|
| **Stream chunks during hold** (recommended) | Hold red circle the whole time; release when done | POST every **1–5 s** of PCM (or Opus later) to `/v1/messages` or a diary-style route; server **stitches** one inbox item for the parent | **One chunk** (~1–5 s), not the full diary |
| **h22-style session** | Unmute latch = “recording”; mute = stop | Already POSTs **~1 s** WAV chunks to `/v1/diary` | One second |
| **Record-all-then-POST** | Hold, release, wait | Single blob after release | **Entire message** (+ timeout risk) |

For a **3 minute** hold with **5 s** chunks: ~**36 POSTs** × ~160 KB ≈ **5.8 MB** total — same audio, but each POST stays small (~170 kbps for ~2 s upload budget per chunk on a bad link).

**Opus** (~16 kbps mono) would cut a 3 min message to ~**360 KB** total — worth it before chasing five-minute single POSTs. Not on the wire yet ([`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)).

### Parent playback

Your phone plays **one continuous clip** whether the box sent one WAV or thirty chunks. Stitching, gapless join, and “one notification per diary” are **server jobs** — same as group hangout mixing staying on the server.

### Storage impact of minutes-long notes

| | 10 s demo | 3 min diary (PCM) | 3 min (Opus 16 kbps, future) |
|---|---|---|---|
| One message on server | ~320 KB | ~5.8 MB | ~360 KB |
| Pending in **outbox** (one unsent diary) | ~320 KB | ~5.8 MB | ~360 KB |
| Fits in **~12 MiB** on-chip outbox | ~35 messages | **~2** full diaries | ~30+ |

Long diaries make **outbox + chunk retry** more important, not larger SD cards. A 16 GB card still does not help until firmware writes failed chunks somewhere.

## Size limits (today)

These bounds matter for “will this POST fit?” — not for shopping capacity.

### Device (record-out) — demo firmware only

| Limit | Value | Where |
|---|---|---|
| Max hold length | **10 s** *(demo)* | `MAX_SEC` / `PCM_CAP` in **h08**, **x01** — **not the product target** |
| Max outbound WAV | **~320 KB** | Same |
| Upload HTTP timeout | **15 s** | **h08**, **x01** `post_wav` — too short for multi‑MB single POST |
| Inbound blob download | **320 KB** cap | **h09** `MAX_BLOB` — must rise for long inbound parent clips too |

Product path: **chunked upload during hold** (see above) or Opus; not a bigger `MAX_SEC` alone.

### Server (combined / 03_messages)

| Limit | Value | Notes |
|---|---|---|
| Enforced max blob size | **None in code** | `await blob_item.read()` loads the whole body; practical cap is RAM / reverse proxy |
| Default message TTL | **3600 s** (`FAMILY_TTL_S`) | Combined only; unread counts ignore expired items |
| Inbox list default | **10** messages per `GET` | `limit` query param can change |

A **320 KB** WAV is tiny for the server. Size limits are unlikely to reject a normal clip; **timeouts and connectivity** are the realistic failures.

### Rough uplink math

320 KB in 15 s needs ~**170 kbps** effective throughput (Wi-Fi + TCP + HTTP). Fine on desk LAN; **other-house Wi-Fi or weak signal** can hit the timeout even when “Wi-Fi works” for heartbeat.

## What local storage is actually for

Three different jobs — do not conflate them:

| Role | Purpose | Survives reboot? | Built today? |
|---|---|---|---|
| **Server archive** | Parent inbox, playhead, diary journal | Yes (your disk) | Yes |
| **PSRAM staging** | One take while recording + single POST | **No** | Yes |
| **Outbox** | Pending uploads after failed POST; retry when Wi-Fi/server returns | Only if on **flash FAT** or **removable** media | **No** |

An **outbox** does not need 16 GB. It needs **reliable small writes** and a few dozen clip slots:

| Outbox medium | Practical pending queue | Notes |
|---|---|---|
| **On-chip FAT** (~12 MiB after partition work) | **~2** × 3 min PCM diaries, or **~30+** × 10 s clips | Chunked outbox can retry per chunk; see minutes-long section |
| **USB stick** (dock) | Same order, much headroom | Needs USB MSC firmware; **conflicts with camera** on one USB-A port |
| **SENSOR microSD** | Same | Needs SDMMC firmware; **replaces dock** |
| **PSRAM only** | 1 clip | Lost on reset; not a real outbox |

Product direction (not implemented): write WAV to outbox → POST → delete on **200**; background retry on heartbeat / Wi-Fi up; show **“1 not sent yet”** on locked idle (count only, no body — same privacy rule as inbound).

**Diary (h22)** is the partial exception: **~1 s chunks** POST independently; one failed second is lost but the session continues. That is **streaming upload**, not a durable outbox.

**Device log (h24)** is the first **event outbox** prototype: timestamped lines (boot, Wi-Fi, mute, buttons, peer changes) ride on `/v1/heartbeat` and land in `data/h24_device_log/` on the host. The box keeps an in-RAM ring until `logs_ack`; **reboot clears unsent lines**. When you add SD or on-chip FAT, the same ring + ack cursor in NVS is the shape to persist across power loss — same problem as voicemail outbox, smaller payloads.

## Before you shop

| Where media lives today | Buy a card? |
|---|---|
| **Server disk** (`data/` under combined / server demos) | No — use the Mac’s SSD. **Back this up** if you care about clips. |
| **PSRAM (16 MiB)** | No — one take in flight; **not** an outbox; gone on reset. |
| **SPI flash (16 MiB)** | No card — soldered. Could host **~12 MiB outbox FAT** after firmware work. |
| **Removable on the box** | **Optional** — only helps **after outbox firmware** exists; see below. |

The gold-finger connector accepts **one** accessory at a time. You cannot sit on the **DOCK** (USB-A) and the **SENSOR** perch (microSD) at the same time without a custom adapter.

| Kit on the desk | Removable slot | What to buy |
|---|---|---|
| **BOX-3B** (main + **DOCK**) — recommended | **Dock USB-A** only | **USB flash drive** (optional). No microSD slot on this SKU. |
| **BOX-3 full** + **SENSOR** brick | **microSD** in SENSOR | **microSDHC** card (optional). Loses dock USB-A while mounted on SENSOR. |

For a normal v1 tryout (combined + **x01**): **cards do not fix upload failures yet** — there is no outbox code. Buy removable media when you implement or test **outbox on USB/SD**; until then, keep the **server reachable** on the LAN.

---

## microSD (ESP32-S3-BOX-3-SENSOR only)

Espressif’s **SENSOR** accessory has a **microSD slot**. User guide and hardware overview: **up to 32 GB**. There is **no microSD** on BOX-3B, on the **DOCK**, or on the main unit by itself.

### Slot vs card vs “SDMMC firmware”

Three different things people mix up:

| | What it is | Do you have it? |
|---|---|---|
| **microSD slot** | Physical connector on the **SENSOR** brick (gold-finger perch) | Only if you bought the **full BOX-3** kit or the SENSOR accessory separately — **not** on BOX-3B |
| **microSD card** | Removable **storage you buy** (16 GB HC, FAT32) and **insert** in that slot | Optional purchase — see specs below |
| **SDMMC firmware** | **Software in our ESP-IDF app** — ESP-IDF’s `sdmmc` host driver + FatFs mount so the box can read/write files on the card | **Not in this tree yet.** You flash it **the same way as h08 or x01** (`make flash …`); there is no separate “SDMMC chip” or factory update for the slot |

The slot is **already wired** on SENSOR (SDMMC on GPIO9/11/12/13/14/42). Nothing extra to solder. What is missing is **product/demo code** that calls `esp_vfs_fat_sdmmc_mount()` (or equivalent) and uses the mount for an outbox or logs.

That is the same class of work as **USB stick** support: hardware exists on the accessory, **host driver + filesystem mount** is firmware we develop and ship in the next flash — not a different SKU of BOX-3.

**BOX-3B on the desk today:** no microSD slot in use. Removable path on that kit is **USB-A on the DOCK** (also needs firmware we have not written). **On-chip flash FAT** is a third path — no card at all, but a custom partition table in firmware.

### Recommended purchase

| Spec | Buy this | Why |
|---|---|---|
| **Capacity** | **16 GB** | Sweet spot: native **FAT32**, well within the 32 GB spec, cheap. Enough for weeks of dev logging or thousands of clips. |
| **Card type** | **microSDHC** (not SDXC) | HC cards (4–32 GB) are almost always FAT32 out of the box. SDXC (64 GB+) often ships **exFAT**, which ESP-IDF FatFs **does not mount by default**. |
| **Speed rating** | **Class 10** or **UHS-I U1** | This product writes **small sequential files** (16 kHz PCM ≈ **32 KB/s**; 1 s diary chunks ≈ **32 KB**). You are not recording 4K video. Class 10 is plenty. |
| **Bus** | Any card that works in **SDMMC 4-bit** | SENSOR wiring uses the S3’s SDMMC pins (not SPI). Mainstream SanDisk / Samsung HC cards are fine. |
| **Brand** | **SanDisk Ultra** or **Samsung EVO Select** (16 GB HC) | Avoid no-name cards for bring-up. Some **32 GB** SanDisk Ultra cards have failed `sdmmc_card_init` in community reports — if you must use 32 GB, **format FAT32 yourself** and keep a 16 GB spare for debug. |

### Do not buy (microSD)

| Avoid | Reason |
|---|---|
| **64 GB+ SDXC** as “future proof” | Needs **exFAT** (or a small FAT32 partition). exFAT support in IDF is **off unless you change `ffconf.h`**. |
| **A2 / V30 / “video” cards** for a premium | Write speed above U1 is unused. The bottleneck is Wi-Fi upload and (on SENSOR) SDMMC tuning, not the card’s peak MB/s. |
| **UHS-II** | Wrong interface; no benefit. |
| **SD adapter–only full-size SD** without microSD | Slot is **microSD**. |
| **“Surveillance” / overwrite-optimized** | OK if FAT32, but unnecessary cost. |

### Format before first use

Format on the Mac (or the box once firmware mounts it):

1. **Scheme:** **MS-DOS (FAT32)** — not APFS, not exFAT, not NTFS.
2. **Allocation unit size:** **32 KB** (or default). ESP-IDF SD mounts use **512-byte sectors**; FAT32 with 32 KB clusters is normal for 16 GB.
3. **Volume label:** optional (e.g. `BOXA`).

Disk Utility: select the card → Erase → Format **MS-DOS (FAT)** → GUID partition map is fine for the **whole card** at 16 GB.

CLI:

```bash
# Replace diskN with the correct identifier from `diskutil list`
diskutil eraseDisk FAT32 BOXA MBRFormat /dev/diskN
```

Verify: `diskutil info /dev/diskN` should show **File System Personality: MS-DOS FAT32**.

### How much 16 GB actually holds (this project’s formats)

Order-of-magnitude, if firmware ever wrote raw blobs locally:

| Content | Size | On 16 GB |
|---|---|---|
| 10 s PCM voicemail (16 kHz s16le) | ~320 KB | ~50 000 clips |
| 320×240 JPEG still | ~16 KB | ~1 000 000 stills |
| 10 s QVGA MJPEG @ 10 fps | ~1.2 MB | ~13 000 clips |

You will run out of **patience** before you run out of 16 GB. Capacity is not the reason to buy 32 GB.

### Firmware note

**No demo mounts SENSOR microSD or USB MSC for an outbox.** Buying a card is ahead of that firmware. SENSOR **replaces the DOCK** — you lose **USB-A** (camera / USB stick) while on that perch.

SD pins on SENSOR (IO9/11/12/13/14/42) overlap the **Pmod I2S1** map on the DOCK ([`SPEAKER.md`](SPEAKER.md)). In practice you pick **dock audio** or **SENSOR storage**, not both.

---

## USB flash drive (BOX-3-DOCK USB-A)

BOX-3B’s realistic removable path is a **USB mass-storage stick** on the **dock USB-A** port. The port is **USB 1.1 full-speed** host (**12 Mbps** ≈ **1.5 MB/s** theoretical; expect **~0.3–1 MB/s** sustained writes in practice).

### Recommended purchase

| Spec | Buy this | Why |
|---|---|---|
| **Capacity** | **16 GB** (8 GB OK; **32 GB** OK if FAT32) | Outbox needs **megabytes**, not gigabytes; 16 GB is cheap and FAT32-safe. |
| **USB generation** | **USB 2.0** stick (backward compatible) | Host is **full-speed**. A USB 3.x stick works but **will not go faster** than ~12 Mbps on this port. |
| **Filesystem** | **FAT32** | ESP-IDF USB MSC + FatFs path expects FAT32 for large sticks. FAT16 only if ≤2 GB. |
| **Class / tier** | Any **mainstream** stick | “USB 3.2 Gen 2” speed ratings are irrelevant; bus is 1.1. |
| **Form factor** | **Low-profile** (“nano”) | Box on desk stand; a long stick sticks out and gets bumped. |
| **Features** | **Plain MSC** — no encryption suite, no dual-LUN “Public/Secure” | Odd firmware on secure sticks sometimes fails enumeration on embedded hosts. |

### Do not buy (USB stick)

| Avoid | Reason |
|---|---|
| **Only exFAT / NTFS formatted** | Reformat to FAT32 on the Mac before plugging into the box. |
| **Hardware-encrypted** drives (some Kingston/Lexar “Vault”) | May not enumerate as simple MSC. |
| **USB-C–only** stick without A adapter | Dock port is **USB-A**. |
| **Hub built into the stick** | Unnecessary; if you add a hub later, **12 Mbps is shared** between camera + disk + hub. |

### Format (same as microSD)

**MS-DOS (FAT32)**, one partition, MBR is fine. Label e.g. `BOXCACHE`.

### Port conflict (important)

USB-A is **one** host port:

- **UVC camera** ([`UVC-CAMERA.md`](UVC-CAMERA.md)) **or** **USB stick** — both fit only with a **USB 2.0 hub**, and the hub shares **12 Mbps** with everything plugged in.
- OK: snapshot → unmount → copy stick on the Mac.
- Not OK: live camera stream + continuous stick logging at full rate.

Firmware **does not mount USB MSC** in this tree yet. Buy when implementing **outbox on USB**; not a fix for today’s **x01** upload gap.

---

## On-chip flash (nothing to buy)

The WROOM-1 module has **16 MiB** soldered flash. Today **~14.4 MiB is unallocated** — not a volume until you flash a custom partition table (~**11–12 MiB** wear-leveled FAT after a 2–3 MiB app). That is a **firmware change**, not a shopping trip.

---

## Server disk (where the inbox actually goes)

Combined host and server demos write under `data/` (messages, diary, playback assets). Any Mac SSD with **a few GB free** is enough for months of desk testing.

Rough growth:

| Traffic | Per item | 1 GB holds |
|---|---|---|
| 10 s WAV voicemail | ~320 KB | ~3 000 clips |
| Downscaled JPEG | ~20–80 KB | ~12 000–50 000 photos |

No special speed class. Back up `data/` if you care about the clips.

---

## Shopping checklist

### Minimum (current product tryout)

- **Nothing for the box** that fixes failed uploads — outbox is not coded.
- **Server disk** on the Mac with headroom; don’t let the host sleep mid-test.

### When outbox firmware exists (BOX-3B + DOCK)

| Qty | Item | Spec |
|---|---|---|
| 1–2 | **USB flash drive** | **16 GB**, USB 2.0, **FAT32**, low-profile. One per box if two kits. |
| 0 | microSD | **Not used** on BOX-3B. |

### If you also have the SENSOR brick (full kit)

| Qty | Item | Spec |
|---|---|---|
| 1–2 | **microSDHC** | **16 GB**, **Class 10** / **U1**, **FAT32**. |
| 0–1 | USB stick | Only while on **DOCK**; not while box sits on SENSOR. |

### Format both once on the Mac

**MS-DOS (FAT32)**. Do not ship exFAT cards to the other house expecting the ESP32 to mount them without extra IDF config.

---

## Related

- Upload failure behavior in firmware: **h08**, **x01** `do_record`, **h22** diary chunks
- Silicon map: [`HARDWARE.md`](HARDWARE.md)
- USB camera vs stick on one port: [`UVC-CAMERA.md`](UVC-CAMERA.md)
- Playhead on server: [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md) h10
- Inspect partitions from the Mac: `python scripts/device.py storage`
