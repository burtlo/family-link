# Feature: Settings, profile, and sleep

| Field | Value |
|-------|-------|
| **Status** | **Implemented** (firmware + server profile API); **Partial** (sleep/ambient vs full BOX-UI qualification) |
| **Areas** | Firmware, server, web twin |
| **Last updated** | 2026-10-08 |

## Purpose

Per-user identity (accent, avatar slot), volume control, sign-out, and bedroom-safe idle dim/sleep without killing the signed-in session.

## UX

- Shoulder from carousel → settings carousel: volume, color swatches, face/avatar, sign out.
- Sleep: 2 min dim → 5 min ambient sleep screen with breathe animation; wake on touch or short circle tap.
- Mute latch: privacy screen + mic gate (hardware GPIO).

## Behavior

- `PUT /v1/profile` persists `avatar_slot`, `accent_hex`, `autoplay_new` in SQLite.
- Server rejects invalid profile payloads (`test_legacy_upload_stream_range_restart_state` profile round-trip).
- Idle timers driven by `v1_timing.h` constants shared with web twin.

## Implementation

| Layer | Location |
|-------|----------|
| Settings UI | `firmware/v1/v1_ui_common.c`, `x02_main.c` |
| Profile API | `server.py`, `user_mailbox.set_profile` |
| Twin | `box.js` settings modals |

## Constraints

- Connecting / Wi-Fi error screens must not enter ambient sleep (spec).
- 64 KB LVGL heap budget (project rule).

## Verification

- Server profile persistence across restart in API/archive tests.
- Firmware: manual sleep/wake and settings save.

## Remaining Work

- Full BOX-UI sleep visual qualification (badge pulse, accent glow).
- Autoplay-new behavior end-to-end if profile flag unused on device.
