# Packed profile portraits (generated)

**Do not edit by hand.** Produced by:

```text
make avatars
```

| Artifact | Role |
|----------|------|
| `portrait_<id>.c` | `LV_COLOR_FORMAT_RGB565A8` image descriptor + pixel map |
| `portraits_registry.c` / `.h` | `fl_portrait_lookup("id")` for firmware |
| `<id>.bin` | LVGL image binary (verification artifact) |

Source manifest: [`docs/product/device-ui/avatars/avatars.json`](../../../docs/product/device-ui/avatars/avatars.json).

LAB-14 (`CAT-20`) and other firmware link these files via CMake `file(GLOB portrait_*.c)`.
