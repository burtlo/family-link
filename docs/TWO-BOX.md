# Two boxes through the server

The protocol already treats every endpoint as `device_id` + bearer token. A second ESP32-S3-BOX-3 is the same firmware as the first. There is **no** box-to-box Wi-Fi stream.

## Tokens

`devices.example.yaml`:

| Kit | Who | `DEMO_DEVICE_ID` | `DEMO_DEVICE_TOKEN` | Peer |
|---|---|---|---|---|
| Child A | Mazi | `box-a` | `change-me-a` | box-b |
| Child B | Arlo | `box-b` | `change-me-b` | box-a |

Display names (`name: Mazi` / `Arlo`) are for the open-line demos. Protocol ids stay `box-a` / `box-b`.

Flash with **`WHO=`** so each kit gets a different id. The firmware is **compiled once**; Mazi vs Arlo is stamped into that same `.bin`. `secrets.h` stays the Wi-Fi / server file.

`/dev/cu.usbmodem*` is the USB **jack**, not the kit — the number moves if you replug. The first flash of each kit with `PORT=` remembers the USB serial in gitignored `kits.local.yaml`. After that, any jack:

```
make flash DEMO=h26 WHO=mazi
make flash DEMO=h26 WHO=arlo
```

First time both are plugged in (or to re-bind after deleting `kits.local.yaml`):

```
make flash DEMO=h26 WHO=mazi PORT=/dev/cu.usbmodem1101
make flash DEMO=h26 WHO=arlo PORT=/dev/cu.usbmodem101
```

(`ls /dev/cu.usbmodem*` if you still need PORT=. Same `WHO=` for **h20** / **h21** / **h22** / **h26** / **h27**.)

## Mazi ↔ Arlo open line (h20–h22)

Mute latch is the gate: **down (red LED on) = away**, **up (LED off) = open**. Start both kits muted. Plan: [`plans/mazi-arlo-open-line.md`](plans/mazi-arlo-open-line.md).

```
# terminal 1 — pick one host and leave it running
python -m demos.server.h20_presence.server --host 0.0.0.0 --port 8080
# python -m demos.server.h21_talk.server --host 0.0.0.0 --port 8080
# python -m demos.server.h22_diary.server --host 0.0.0.0 --port 8080
# python -m demos.server.h26_draw.server --host 0.0.0.0 --port 8080
# python -m demos.server.h27_sketch.server --host 0.0.0.0 --port 8080

# each kit (USB-C on the box; WHO= selects Mazi vs Arlo)
make flash DEMO=h21 WHO=mazi
make flash DEMO=h21 WHO=arlo
```

- **h20** — each heartbeat sends our open/away; the reply is the friend’s last state.
- **h21** — unmute streams live voice both ways through the server. Friend card follows their mute latch. Volume slider is mute, then 78–100 by 2. **Desk success 2026-08-24:** two kits talk to each other through the host. Next: same-time two-way with Audrey (two rooms; same desk will echo).
- **h22** — unmute records ~1 s chunks; the server stamps UTC. Files under `data/h22_diary/`.

Host smokes (no box): `make demo-presence`, `make demo-talk`, `make demo-diary`.

## Shared drawing (h26)

Finger on one glass, ink on the other. Both kits run the same firmware. Screens start black; coordinates ride `/v1/ws` as JSON `stroke` / `clear`. **Boot** wipes both. Your strokes paint white; the friend’s paint blue.

**Desk success 2026-08-28. High impact.**

```
python -m demos.server.h26_draw.server --host 0.0.0.0 --port 8080
make flash DEMO=h26 WHO=mazi
make flash DEMO=h26 WHO=arlo
```

Host smoke (no box): `make demo-draw`.

## Drawing note (h27)

Record a drawing, send it, watch it play back at the same speed. The friend does not need to be at the glass when you send.

```
python -m demos.server.h27_sketch.server --host 0.0.0.0 --port 8080
make flash DEMO=h27 WHO=mazi
make flash DEMO=h27 WHO=arlo
```

Home: **draw** starts a recording. Finger on the glass is captured with timestamps. **Boot** sends. The other kit shows **new message**; tap to replay. Boot during playback stops and returns to home (the note stays until it finishes).

**Desk success 2026-08-28. High impact.**

Host smoke (no box): `make demo-sketch`.

## Product hangout (h11 / x01)

```
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
```

If combined is not up yet, `python -m demos.server.06_audio_relay.server --host 0.0.0.0 --port 8080` is hangout-only.

Start both boxes. One invites (h11 invites on hello); the other should accept a `ring`. Hold mute to talk; speaker plays only while mute is up.

v1 **product** hangout is still you (phone/Mac) + one child. Two boxes here prove the relay. The Mazi/Arlo demos are a separate kid-to-kid experiment. Inboxes stay per child.
