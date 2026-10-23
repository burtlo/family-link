# P9 hardening notes

Operator-authorized closure of residual verifier gaps (desk volume L1 + device button wiring).

## Gaps closed

| # | Gap | Closure |
|---|-----|---------|
| 1 | Physical BOOT / mute / red not routed on `client/device` | `product_hardware_buttons.c` polls BSP `iot_button` into `product_inbox_on_boot_pressed`, `product_volume_on_mute`, `product_volume_on_red_record` (+ login mute fallback) |
| 2 | EXP-006 / EXP-008 signed-in BOOT return not in L1 | `signed-in-boot` in `product_volume_model_test.cpp` |
| 3 | Selection / play head across Volume leave+re-enter | `boot-roundtrip`, `play-head-boot` |
| 4 | EXP-017 restart after failed persist | `restart-persist` |
| 5 | EXP-009 speaker hook undocumented for L3 | `set_speaker_codec_callback` on `DeskVolumeJourney`; device bridge registers hook; optional L1 `speaker-callback` |

## EXP-SETTINGS row → L1/L2/L3 proof

| EXP | Meaning (summary) | Test / probe |
|-----|-------------------|--------------|
| EXP-001 | Volume-only signed-in settings surface | `signed-in-boot`, device volume UI attach |
| EXP-002 | Split pane, `Volume` header | device bridge title label |
| EXP-003 | Body is 13-stop slider | `stop-table`, device slider range |
| EXP-004 | Save on BOOT or mute leave | `boot-roundtrip`, `mute-signout` |
| EXP-005 | Re-open shows last saved stop | `boot-roundtrip`, `restart-persist` |
| EXP-006 | BOOT from Volume returns to inbox signed in | `signed-in-boot` |
| EXP-007 | Re-enter shows saved stop | `boot-roundtrip` |
| EXP-008 | BOOT from signed-in inbox enters Volume | `signed-in-boot`, `playback-pause` |
| EXP-009 | Speaker follows slider immediately | `speaker-callback` (L1); L3 device audio |
| EXP-010 | Desk-local volume shared across users | `boot-roundtrip` storage semantics |
| EXP-011 | Exactly 13 stops | `stop-table` |
| EXP-012 | No numeric codec on screen | `shows_numeric_readout` via journey (always 0) |
| EXP-013 | Mute on Volume saves and signs out | `mute-signout` |
| EXP-014 | Red record ignored on Volume | `red-ignored` + `product_volume_on_red_record` |
| EXP-015 | Connectivity loss keeps slider | `red-ignored` (`note_connectivity_lost`) |
| EXP-016 | First run default stop 100 | `persist-fail` / `stop-table` default index 12 |
| EXP-017 | Persist fail still leaves; restart restores last good NVS | `persist-fail`, `restart-persist` |

Carousel `EXP-036` (pause on Volume entry, same-session play heads): `playback-pause`, `play-head-boot`.

## Implementation touches

- `client/device/main/product_hardware_buttons.c` — BSP button → product event stubs.
- `fl_product_volume/session.cpp` — `set_speaker_codec_callback`.
- `fl_product_inbox/carousel.cpp` — advance `play_head_ms` while playing for round-trip L1.

## Regression battery

```bash
python -m unittest scripts.tests.test_product_volume_model scripts.tests.test_product_inbox_model
```
