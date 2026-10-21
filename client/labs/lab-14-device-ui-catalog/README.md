# LAB-14 — Device UI catalog

Version: `LAB-14/v1 = firmware lab14-fw-<variant>-v1 + server none + fixture device-ui-catalog/v1`.
Target: ESP32-S3 BOX-3, 320 × 240 RGB565.

This is a **visual review image**, not a login or performance experiment. It
mounts the current [`docs/product/device-ui`](../../../docs/product/device-ui/README.md)
layouts, panels, and screen states from a YAML catalog so you can flash the
defined composition and iterate on it.

The Markdown specs stay the review surface. The YAML catalog is the machine
twin the device interprets. See
[`docs/product/device-ui/catalog/README.md`](../../../docs/product/device-ui/catalog/README.md).

## Operator commands

```text
make lab.lab14
make lab.lab14.scripted
make lab.lab14.build
```

`make lab.lab14` always runs `idf.py build` before flash. An unchanged tree is
incremental; firmware edits compile on that same command.

Boot marker: `LAB14_READY build=<id> scenes=<n> theme=THEME-DARK`.

## On the device

1. The catalog index lists every packed scene as `CAT-NN` plus its title.
2. Tap a row to mount that scene at native 320 × 240. A corner badge repeats
   the same `CAT-NN` so you can name it while looking at the visual.
3. Press **BOOT** to return to the index from any scene.
4. Roster cards accept taps to change focus when the scene allows touch.
5. PIN keys add marks on the partial-PIN scene.

IDs: [`CAT-01`–`CAT-16`](../../../docs/product/device-ui/catalog/README.md#scene-ids).

Serial (USB console), one command per line:

```text
list
scene CAT-01
next
prev
index
```

`LAB14_SCENE CAT-01` and `scene cat-01` are accepted too.

## What is packed

CMake embeds `generated/catalog.json`. If the repo `.venv` exists, configure
refreshes that pack from the YAML catalog. ESP-IDF's own venv is not used
(it does not have the host catalog deps). After editing YAML without a repo
venv, refresh the pack before `idf.py`:

```text
python scripts/device_ui_catalog.py pack
```

Adding a screen that reuses existing panels is a new `scenes/*.yaml` file plus
a rebuild. New panel types need a C renderer in `lab14_ui.c`.

Host check without a kit:

```text
python scripts/device_ui_catalog.py validate
```

## Stand-ins and limits

This image does not approve draft specs. Card colors, initial avatars, button
fill, keypad dim, and `LAYOUT-FULL-CANVAS` are catalog stand-ins documented in
the catalog README. Motion (cover-down, carousel snap, status crossfade) is not
implemented. Live YAML push without reflash is not implemented yet; scene
switching among the packed catalog is.

Scripted mode walks every scene and prints `LAB14_TRACE` then
`LAB14_HARNESS_DONE`. That is a smoke walk, not visual approval.
