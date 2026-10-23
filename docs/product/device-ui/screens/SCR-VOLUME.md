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

## OPEN items

| ID | Question |
|---|---|
| `UI-OPEN-001` | Slider track and thumb pixels (LAB-14 uses catalog stand-in) |
