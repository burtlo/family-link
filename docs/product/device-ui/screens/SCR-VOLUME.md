# Volume screen

ID: `SCR-VOLUME`
Type: Screen composition
Revision: 0.1
Status: Draft
Owner: Product owner
Target: v2 fixed-hardware 320 × 240 landscape canvas
Experience: [`EXP-SETTINGS`](../../experiences/settings.md)

## Purpose

Dedicated desk volume control opened from the inbox with physical BOOT. Hub
settings (logout, icon, color) are out of scope.

## Composition tree

```text
SCR-VOLUME
└─ LAYOUT-SPLIT-PANE
   ├─ upper → PANEL-HEADER-TITLE-LEFT
   └─ lower → PANEL-BODY-VOLUME-SLIDER
```

## Experience authority

| Element | Value | Authority |
|---|---|---|
| Heading | `Volume` | `DEC-008`, `EXP-011` |
| Body | Volume slider; **no numeric readout** | `EXP-SETTINGS`, v1 codec number not adopted |
| Connectivity loss | Slider remains visible | `EXP-014` |

## Catalog scenes

`CAT-37`.

## Implementation invariants

Firmware rules for `client/device/`. Behavior and copy stay in the sections
above; cross-screen rules are in
[device-presentation-invariants](../../../standards/device-presentation-invariants.md).

| Aspect | Rule |
|---|---|
| Screen | `PRODUCT_SCREEN_VOLUME`: signed in and shell stage `VOLUME`. |
| Owner | `product_volume_bridge.cpp` → `render_volume()`; visibility only from `product_volume_set_visible`. |
| Visible surfaces | Upper: volume heading. Lower: slider. |
| Input | Slider `VALUE_CHANGED` → `set_stop_index` (intent only). BOOT → `product_volume_on_boot_leave` via the button poll. RED leaves volume first, then routes to send. |
| Enters when | BOOT on the inbox (`product_volume_on_boot_from_inbox` sets shell `VOLUME`). |
| Leaves when | BOOT → inbox (shell `INBOX`). Mute → login screen; `enter_muted` runs the volume mute path. |
| Background work | None beyond rendering while active; inbox fetch pauses because the inbox is not visible. |
| Update levels | Level 1 slider value only. |
| Never | Volume code hiding or showing the inbox. The slider visible after leaving the volume stage. |

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | Slider track and thumb pixels (LAB-14 uses catalog stand-in) |
