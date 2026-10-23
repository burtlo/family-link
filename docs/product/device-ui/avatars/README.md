# Profile avatar sources (host)

Drop **transparent** bust **PNG / JPEG / WebP** files in **this directory** (not in subfolders) for firmware packing.

| File | Role |
|------|------|
| `*.png` / `*.jpg` / … | Input art (must include real alpha) |
| `avatars.json` | Manifest: optional per-id `palettes`; `portraits` lists **only** successful conversions (maintained by `make avatars`) |

Portrait id is derived from the filename stem (e.g. `arlo.png` → `arlo`).

## Pack for device

```text
make avatars
```

Uses LVGL’s `LVGLImage.py` from LAB-14 **managed components** (not committed). On first pack,
if that script is missing, `make avatars` runs a lightweight `idf.py reconfigure` for LAB-14 to
download dependencies (requires `make install` / ESP-IDF on the host). You can also run
`make lab.lab14.build` once beforehand.

Writes RGB565A8 LVGL C assets to [`client/assets/portraits/`](../../../client/assets/portraits/README.md) and regenerates catalog scene **`CAT-20`** (one roster card per successful portrait).
Failures are printed; failed sources are omitted from `avatars.json`.
Also regenerates **`CAT-20`** YAML, repacks LAB-14 **`generated/catalog.json`**, and rewrites **`portrait_pack_sources.cmake`**.

**Dynamic cleanup:** removing a source PNG and running `make avatars` drops that portrait from `avatars.json`, deletes its `portrait_*.c` / `.bin`, prunes the registry, and removes its roster card from CAT-20. Run **`make avatars` before `make lab.lab14`** so the embedded catalog matches; `make lab.lab14` alone repacks the catalog from YAML at configure time but does not add/remove portraits unless sources changed.

Force reconversion (ignore source SHA cache): `python3 scripts/pack_profile_avatars.py --force`.
