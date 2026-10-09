# Phase 1 changelog — LVGL feature flags (x02)

## Build wiring (`firmware/CMakeLists.txt`)

- Merge `sdkconfig.defaults` + `sdkconfig.defaults.v1` for `x02_product_shell`, `x02_opus_size_probe`, `x02_opus_dep_only`.
- Set `SDKCONFIG` to `${CMAKE_BINARY_DIR}/sdkconfig` for those demos so **local `firmware/sdkconfig` (island/menuconfig) does not override** v1 defaults. Without this, `sdkconfig.defaults.v1` loads but has no effect on symbols already set in `firmware/sdkconfig`.

## Flag groups in `sdkconfig.defaults.v1`

| Group | Action |
|-------|--------|
| Demos / examples | `CONFIG_LV_BUILD_DEMOS=n`, `CONFIG_LV_BUILD_EXAMPLES=n` |
| Unused widgets | ANIMIMG, ARC, ARCLABEL, BUTTONMATRIX, CALENDAR, CHART, CHECKBOX, DROPDOWN, IMAGEBUTTON, KEYBOARD, LED, LINE, LIST, MENU, MSGBOX, ROLLER, SCALE, SPAN, SPINBOX, SPINNER, TABLE, TABVIEW, TEXTAREA, TILEVIEW, WIN, BARCODE, QRCODE, GIF → `n` |
| Kept (v1 uses) | BUTTON, CANVAS, IMAGE, LABEL, SLIDER (+ BAR), SWITCH, FLEX/GRID/themes/draw_sw (unchanged) |
| Fonts (Phase 0 parity) | Montserrat **14 only** in this fragment until Phase 2 replaces the block — matches effective pre-change `firmware/sdkconfig` override |

## Island smoke

`idf.py -D FAMILY_DEMO=p13_ui_showcase -B build/p13_ui_showcase build` — **OK** (no `sdkconfig.defaults.v1` merge).
