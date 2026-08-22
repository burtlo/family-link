# Phases

## Phase 1 — One-to-one desk

Hardware: two **ESP32-S3-BOX-3B** kits (unit + dock), SSID baked in.

On each box:

- Hold to send **audio** to you.
- PIN to see **text**, **small photos**, and play **audio** from you.
- Locked idle: count only.

On your phone:

- Pick a child.
- Send text, a clip, or a photo (server downscales).
- Start / end a **PTT hangout** with that one box.

Child → you photos wait unless a USB camera is on the dock.

## Phase 2 — Hangout that matches Minecraft night

Same hardware. If the mute key is too small, Pmod arcade button.

- You tap Start; box shows you are here.
- Half-duplex PTT while they play.
- Still one child at a time if you want it simple; or both boxes can join **you** (see phase 3) once mixing exists.

## Phase 3 — All join

- Server mixes you + N boxes into one hangout.
- Inboxes stay **per child** (photos and voicemail are never a group dump unless you choose a family drop later).
- Boxes do not become speakerphones.
