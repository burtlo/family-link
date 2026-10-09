# Plan: LVGL slimming for v1 client (x02)

| Field | Value |
|-------|-------|
| **Doc kind** | `refactor-plan` / `size-optimization` |
| **Owners / areas** | `firmware/v1/`, ESP-IDF sdkconfig, build (`scripts/flash.py`, `Makefile`) |
| **Status** | **`partial` shipped** (2026-10-08) — Phases 0–3 desk-complete; canonical config in `sdkconfig.defaults.v1`; Phase 2 operator re-flash optional |
| **Targets** | Reduce x02 flash footprint from LVGL (features + fonts) without changing product behavior |
| **Last updated** | 2026-10-08 (findings + font inventory) |
| **Supersedes / superseded by** | Complements [`x02-opus-partition-feasibility.md`](x02-opus-partition-feasibility.md); does not authorize partition migration or Opus in x02 |
| **As-built** | Phase 0–2 evidence + [Phase 3 decision](../evidence/v1-lvgl-slimming/phase3-decision/summary.md) |

## At a glance

x02 is **flash-tight** (~96% of the `SINGLE_APP_LARGE` slot per [Phase 1 evidence](../evidence/x02-opus-partition/phase1-summary.md)). **LVGL** (`liblvgl__lvgl.a`) is the largest single library in that image. Today **all** demos share [`firmware/sdkconfig.defaults`](../firmware/sdkconfig.defaults), which enables **Montserrat 14–48** for island showcase **p13** while **v1 only uses six sizes** (14, 16, 22, 24, 28, 32).

This plan runs **three gated phases**: measure baseline → trim LVGL **feature flags** (with device validation) → **font diet** for x02 only (with device validation). Each phase produces committed size artifacts and a short operator sign-off before the next phase starts.

| Phase | Outcome | Status |
|-------|---------|--------|
| [0 — Measure and lock baseline](#phase-0--measure-and-lock-baseline) | Reproducible x02 size snapshot on current config | `done` |
| [1 — LVGL feature flags](#phase-1--lvgl-feature-flags) | Disable unused `CONFIG_LV_USE_*` / decoders; flash + user UX pass | **done** (operator **2026-10-08**) |
| [2 — Font diet (x02)](#phase-2--font-diet-x02-only) | x02 build links only required Montserrat sizes; flash + user UX pass | **done** (config shipped; operator re-flash optional) |
| [3 — Record decision](#phase-3--record-decision) | Delta summary, risks, follow-ups (Opus / partition) | **done** (2026-10-08) |

---

## Goals and non-goals

### Goals

- Recover **flash** in the x02 app image (`.text` + `.rodata`, especially font glyph tables).
- Keep **operator-visible behavior** aligned with [`docs/BOX-UI.md`](../BOX-UI.md) and [`docs/plans/v1-product-spec.md`](v1-product-spec.md).
- Preserve ability to build **island demos** (e.g. **p13** full font ladder) without forcing them onto the x02 font set.

### Non-goals

- Replacing LVGL with another UI stack.
- Changing carousel/record/auth logic except where a disabled LVGL feature forces a compile fix.
- Production **partition table** changes (see x02-opus-partition).
- Automated touch harness (see future QA plan); this plan uses **human device validation** gates.

---

## Background

### What v1 actually uses (LVGL surface)

Static audit of `firmware/v1/*.c` (2026-10-08):

| Category | Used in v1 |
|----------|------------|
| **Widgets / core** | `lv_obj_*`, `lv_label`, `lv_image`, buttons, scroll containers, styles, gradients, animations, timers, events |
| **Fonts (Montserrat)** | **14**, **16**, **22**, **24**, **28**, **32** only — see module table below |
| **Not referenced in v1** | chart, calendar, keyboard, tabview, tileview, roller, table, msgbox, etc. |

| Module | `lv_font_montserrat_*` references |
|--------|-----------------------------------|
| `v1_carousel.c` | 14, 22, 24 |
| `v1_auth.c` | 22, 24, 28, 32 |
| `v1_connect.c` | 16, 22, 24 |
| `v1_record.c` | 16, 22, 24, 28 |
| `v1_ui_common.c` | 28 |

**Note:** [`sdkconfig.defaults`](../firmware/sdkconfig.defaults) comment says “x02 uses 28/48” but **v1 does not reference 48**; 48 is for **p13** / **h02** island demos.

### Shared config problem

One `sdkconfig.defaults` applies to **every** `FAMILY_DEMO` build under `firmware/`. Slimming for x02 should **not** break `make flash DEMO=p13` without an explicit split (see [Build isolation](#build-isolation-x02-vs-island-demos)).

### Prior art

- Size methodology: [`docs/evidence/x02-opus-partition/`](../evidence/x02-opus-partition/) (`idf.py size`, `size-components`, `size-files`).
- LVGL heap policy: [`.cursor/rules/lvgl-incremental-ui.mdc`](../../.cursor/rules/lvgl-incremental-ui.mdc) (64 KiB — **RAM**, not flash).
- Device closure: [`.cursor/rules/device-verify-before-done.mdc`](../../.cursor/rules/device-verify-before-done.mdc).

### Findings — fonts, flash, and desk UX (2026-10-08)

**Count what matters: linked fonts in the x02 binary**, not lines in `sdkconfig.defaults`. Each `CONFIG_LV_FONT_MONTSERRAT_N=y` pulls a glyph table into flash (mostly `.rodata`, on the order of **~14–46 KiB per size** in the Phase 2 build). Verify after `fullclean` with:

`rg 'lv_font_montserrat_' firmware/build/x02_product_shell/family_link_demo.map`

#### Font inventory (starting point → current)

| Stage | What Kconfig *says* (x02) | Montserrat sizes **in the linked image** | Count |
|-------|---------------------------|------------------------------------------|------:|
| **Phase 0 starting point** (baseline commit; no `sdkconfig.defaults.v1`) | Shared [`sdkconfig.defaults`](../firmware/sdkconfig.defaults): **14–48** all enabled | **14 only** — effective x02 build did not ship the full ladder despite defaults | **1** |
| **Phase 1** (LVGL widget trim + v1 fragment) | Same shared defaults; v1 fragment **disables 16–48** | **14 only** — larger UI used `LV_FONT_DEFAULT` / smaller fallbacks | **1** |
| **Phase 2 current** ([`sdkconfig.defaults.v1`](../firmware/sdkconfig.defaults.v1)) | **14, 16, 22, 24, 28** enabled; **32 off** (partition); 18–48 off | **14, 16, 24, 28** linked in map; **22** enabled in Kconfig but often **not linked** when 24 satisfies all `v1_*.c` branches | **4–5** |
| **Island reference** (`p13`, separate `-B`) | Shared defaults **14–48** | Full ladder as built for showcase | **up to 15** |

**Operator note:** Typography on desk looked “flattened” during Phase 1 because **only one real Montserrat** was in flash—not because `v1_*.c` stopped asking for 22/24/28. Phase 2 restores those sizes; that is why flash **rose ~82 KiB vs Phase 1** even though the plan “font diet” means *fewer than p13’s 15 sizes*.

#### Where flash actually moved

| Comparison | App bytes | Takeaway |
|------------|----------:|----------|
| Phase 0 → Phase 1 | **−69,808** | **Main win:** disable unused LVGL widgets / demos / examples (`CONFIG_LV_USE_*`, `LV_BUILD_*`). |
| Phase 1 → Phase 2 | **+81,920** | **Cost of correct typography:** add glyph tables Phase 1 had stripped. |
| Phase 0 → Phase 2 | **+12,112** | Slimmer LVGL code (−`.text`) plus more font rodata (+`.rodata`); net slightly **larger** than pre-slimming baseline. |

Phase 2 is a **diet relative to the 15-size island ladder**, not relative to the **Phase 1 image** operators validated with messages.

#### Further font cuts — low ROI (documented, not scheduled)

Additional sizes *could* be removed (e.g. drop **22** when **24** is always linked; drop **28** and use **24** for titles/PIN with small code edits; re-collapse to **14-only** for maximum flash at the cost of the regression seen in Phase 1). Order-of-magnitude savings per removed size: **~15–46 KiB** rodata each—not enough to change the product story next to:

- **~70 KiB** already recovered in Phase 1 flags, and  
- **~40 KiB** factory headroom left at Phase 2 (**39,616** bytes free).

**Recommendation:** treat Phase 2’s **five-size Kconfig / four-size typical link** as the practical balance unless Opus or partition work forces another pass. **32** remains off until factory grows or code shrinks elsewhere (~6.3 KiB overflow with all six code-referenced sizes). Do not expect another large flash drop from font Kconfig alone without accepting visible typography loss or reflowing UI to fewer point sizes.

---

## Build isolation (x02 vs island demos)

**Decision (Phase 0, locked for Phase 1):** use **`firmware/sdkconfig.defaults.v1`** for x02-only Montserrat + `CONFIG_LV_USE_*` trims; leave **`firmware/sdkconfig.defaults`** as shared platform defaults (Wi‑Fi, partition, PSRAM, TLS insecure flags, **full Montserrat 14–48** for **p13** / islands).

Wire merge in **`firmware/CMakeLists.txt`** via `SDKCONFIG_DEFAULTS` (semicolon-separated list) when `FAMILY_DEMO` is `x02_product_shell` (and x02 size probes: `x02_opus_size_probe`, `x02_opus_dep_only` per `firmware/main/CMakeLists.txt`). Also set **`SDKCONFIG` to `${CMAKE_BINARY_DIR}/sdkconfig`** for those demos — otherwise gitignored **`firmware/sdkconfig`** (island menuconfig) overrides `sdkconfig.defaults.v1` and flags appear unchanged. **`scripts/flash.py`** already uses **`firmware/build/<demo>/`** (`build/x02_product_shell` for product) — island builds must keep using **`build/p13_ui_showcase/`** (etc.) so a stripped x02 `sdkconfig` never leaks into p13 without an explicit rebuild in that dir.

**Phase 1 acceptance:** `make x02` / `idf.py … x02_product_shell` picks up the v1 fragment; `idf.py -D FAMILY_DEMO=p13_ui_showcase -B build/p13_ui_showcase build` still links the full font ladder after `fullclean` in **that** build dir.

---

## Reproducible x02 builds (required for all size phases)

Demos do **not** use the default `firmware/build/` CMake tree. Always pass **`-B build/x02_product_shell`** (same as `scripts/flash.py`).

| Mistake | Symptom |
|---------|---------|
| `idf.py -D FAMILY_DEMO=x02_product_shell fullclean` **without** `-B` | Targets `firmware/build/` (not a CMake dir); **fullclean refuses** (“Directory … doesn't seem to be a CMake build directory”) |
| Reusing another demo’s `-B` dir with a different `FAMILY_DEMO` | Stale/wrong `sdkconfig` — always fullclean **inside** the demo’s build dir |

**Canonical Phase 0/1/2 commands** (from repo root):

```bash
cd firmware
source ~/esp/esp-idf/export.sh   # host-specific

BD=build/x02_product_shell
DEMO=x02_product_shell

idf.py -D FAMILY_DEMO=$DEMO -B $BD fullclean build
idf.py -D FAMILY_DEMO=$DEMO -B $BD size
idf.py -D FAMILY_DEMO=$DEMO -B $BD size-components
idf.py -D FAMILY_DEMO=$DEMO -B $BD size-files

wc -c $BD/family_link_demo.bin
python3 -m esp_idf_size --archives $BD/family_link_demo.map | head -20
```

**Makefile equivalents:** `make build-firmware DEMO=x02` or `python scripts/flash.py --demo x02 --build-only` (no flash).

**Resolved Kconfig (IDF 5.4.2):** there is no `build/x02_product_shell/sdkconfig` file on disk; use `build/x02_product_shell/config/sdkconfig.{json,h,cmake}` for grep of active `LV_*` keys. Phase 0 evidence records Montserrat intent in [`sdkconfig-lvgl-source.txt`](../evidence/v1-lvgl-slimming/phase0-baseline/sdkconfig-lvgl-source.txt) (copy of `sdkconfig.defaults` font block).

---

## Phase 0 — Measure and lock baseline

**Goal.** Establish a **current** x02 flash baseline on the commit under test, independent of the 2026-10-04 x02-opus snapshot.

**Status:** `done` (2026-10-08). Evidence: [`docs/evidence/v1-lvgl-slimming/phase0-baseline/`](../evidence/v1-lvgl-slimming/phase0-baseline/). **No firmware or sdkconfig changes** in this phase.

### Capture record

| Field | Value |
|-------|-------|
| Git | `6d1f8a8f11326af4b9076d5d535828338c84942a` |
| ESP-IDF | v5.4.2 |
| Host | macOS 27.0.0 arm64; Python 3.14.7 (espressif env) |
| Target | `esp32s3` |
| `FAMILY_DEMO` | `x02_product_shell` |
| Build dir | `firmware/build/x02_product_shell` |

### Baseline numbers (canonical)

App partition **1,536,000** bytes (`CONFIG_PARTITION_TABLE_SINGLE_APP_LARGE`).

| Metric | Bytes | Notes |
|--------|------:|-------|
| **App image** (`family_link_demo.bin`) | **1,484,272** | `0x16a5f0`; partition check **3% free** (`0xca10` = 51,728 B) |
| Flash `.text` | 1,101,922 | |
| Flash `.rodata` | 253,684 | primary font glyph sink for Phase 2 |
| DRAM (DIRAM used) | 232,431 | LVGL heap policy separate (64 KiB — RAM) |
| **IRAM used** | **16,383 / 16,384** | **99.99%** — LVGL slimming unlikely to help IRAM |
| **`liblvgl__lvgl.a` flash total** | **312,310** | `esp_idf_size --archives` on `family_link_demo.map` |
| `liblvgl__lvgl.a` DRAM | 66,152 | |

**Top five archives (flash total):** `liblvgl__lvgl.a` 312,310 → `libesp_app_format.a` 115,252 → `libnet80211.a` 107,351 → `libmain.a` 102,628 → `liblwip.a` 99,931.

**Comparison to [x02-opus Phase 1](../evidence/x02-opus-partition/phase1-summary.md) (2026-10-04, `9788f013`):** app bytes, free bytes, `.text`, `.rodata`, and IRAM are **byte-identical** on this tree — confirms the LVGL slimming program starts from the same ~96.6% full factory slot.

### v1 LVGL API inventory (static, for Phase 1)

`rg 'lv_[a-z_0-9]+' firmware/v1 --no-filename -o | sort -u` → **109** distinct symbols (2026-10-08). Dominant families: `lv_obj_*` (layout, scroll, style, flags, gradients), `lv_label_*`, `lv_image_*`, `lv_button_*`, `lv_anim_*`, `lv_timer_*`, `lv_event_*`, `lv_text_get_size`, `lv_screen_active`, `lv_color_hex`. **No** direct use of chart, calendar, keyboard, tabview, tileview, roller, table, msgbox APIs in `firmware/v1/*.c`.

### Baseline LVGL Kconfig surface (enabled widgets, Phase 0)

From `build/x02_product_shell/config/sdkconfig.json` — **`LV_USE_*` true** (48) includes many widgets v1 never calls, e.g. `LV_USE_CALENDAR`, `LV_USE_CHART`, `LV_USE_KEYBOARD`, `LV_USE_TABVIEW`, `LV_USE_TILEVIEW`, `LV_USE_MSGBOX`, `LV_USE_ROLLER`, `LV_USE_TABLE`, `LV_USE_MENU`, `LV_USE_WIN`, `LV_USE_SPINBOX`, `LV_USE_TEXTAREA`, `LV_USE_DROPDOWN`, `LV_USE_LIST`. Also **`LV_BUILD_DEMOS=y`** and **`LV_BUILD_EXAMPLES=y`** (candidates to disable in Phase 1 if they link into x02 — verify per build).

**Montserrat in defaults:** all sizes **14–48** enabled in [`sdkconfig.defaults`](../firmware/sdkconfig.defaults); v1 code references only **14, 16, 22, 24, 28, 32** (Phase 2 target).

### Evidence artifacts

```
docs/evidence/v1-lvgl-slimming/phase0-baseline/
  toolchain.txt
  git-rev.txt
  sdkconfig-lvgl-source.txt   # Montserrat block + note on config/ layout
  x02-size.txt
  x02-size-components.txt
  x02-size-files.txt
  summary.md
```

Parse **`liblvgl__lvgl.a`** from `esp_idf_size --archives` (clearer than the wrapped `size-components` table).

### Acceptance (met)

- Evidence directory committed.
- `summary.md` records app bytes, free bytes, LVGL archive bytes, IRAM.
- No product behavior change.

### Operator gate

None (desk-only). **Proceed to Phase 1** after committing evidence; implement `sdkconfig.defaults.v1` + CMake merge first.

---

## Phase 1 — LVGL feature flags

**Goal.** Turn off LVGL subsystems and image decoders that **v1 does not use**, rebuild x02, flash one kit, and have the **operator confirm** no visual or interaction regression on core journeys.

**Desk status (2026-10-08):** implemented in [`firmware/sdkconfig.defaults.v1`](../firmware/sdkconfig.defaults.v1) + [`firmware/CMakeLists.txt`](../firmware/CMakeLists.txt). Evidence: [`phase1-flags/`](../evidence/v1-lvgl-slimming/phase1-flags/).

### Phase 1 size (vs [Phase 0 baseline](#phase-0--measure-and-lock-baseline))

| Metric | Phase 0 | Phase 1 | Δ |
|--------|--------:|--------:|--:|
| App bytes | 1,484,272 | **1,414,464** | **−69,808** |
| Free bytes | 51,728 (3.4%) | **121,536** (7.9%) | +69,808 |
| `liblvgl__lvgl.a` flash | 312,310 | **245,075** | −67,235 |

**Operator (2026-10-08):** PASS — PIN, carousel, record/send, **messages** on desk BOX-3; no tofu/clipping reported. Island smoke: **`p13_ui_showcase` build OK**.

### Preparation

1. From Phase 0 `sdkconfig`, export the active **`CONFIG_LV_USE_*`** and decoder options (menuconfig: `Component config → LVGL configuration`, or grep `CONFIG_LV` in saved sdkconfig).
2. Build a **used-API inventory** from v1 only:
   ```bash
   rg 'lv_[a-z_]+' firmware/v1 --no-filename -o | sort -u
   ```
   Map symbols to widget types (label, image, btn, scroll, anim, timer, style, gradient).
3. Draft a **disable list** — typical candidates on ESP-IDF LVGL 9.x (verify names in your sdkconfig; **do not guess** without opening menuconfig or `Kconfig`):
   - Unused widgets: calendar, chart, keyboard, list (if unused), menu, msgbox, roller, spinbox, tabview, tileview, win, etc.
   - Unused libs: **PNG/JPEG/BMP/GIF/SJPG** decoders if v1 only uses **built-in** `lv_image` / RGB assets.
   - **3D / vector** paths if enabled and not referenced by v1.
4. Apply disables in **`sdkconfig.defaults.v1`** (not global defaults) one **group at a time** if build fails; document each group in `phase1-changelog.md`.

### Build and size

1. After each stable flag set: `fullclean` + x02 build + `size-components`.
2. Save delta vs Phase 0 in `docs/evidence/v1-lvgl-slimming/phase1-flags/summary.md` (bytes saved, flags changed).

### Flash and device validation (operator)

Flash **one** BOX-3 used for v1 desk work (`make flash DEMO=x02 WHO=…` or `scripts/flash.py --demo x02`).

**Minimum manual script** (from [`BOX-UI.md`](../BOX-UI.md)):

| # | Journey | Pass criteria |
|---|---------|----------------|
| 1 | **PIN** | Roster → PIN → carousel (good PIN); wrong PIN shows `wrong pin`; no infinite `checking...` |
| 2 | **Connecting** | Optional: server down → connecting dots; restore → roster |
| 3 | **Carousel** | ≥2 messages; peek snap; play to end; **same card stays centered** |
| 4 | **Settings** | Shoulder → settings carousel; volume chirp; back to inbox |
| 5 | **Record/send** | Short record to one recipient; send completes; return to carousel |
| 6 | **Visual smoke** | No missing glyphs (tofu), clipped labels, or solid-color images |

Capture **≥20 lines** serial log during PIN success (per device-verify rule). Store private log path in session notes; commit only **pass/fail + date + commit** in evidence README.

### Acceptance

- x02 **links** with v1 sdkconfig fragment.
- **Measurable** flash delta vs Phase 0 (even if small — document honestly).
- Operator marks **PASS** on table above; any FAIL blocks Phase 2 until fixed or flag reverted.
- Island demo **p13** still builds if repo promises dual config (smoke: `idf.py -D FAMILY_DEMO=p13_ui_showcase build` after Phase 2 may be enough; optional here).

### Rollback

Revert `sdkconfig.defaults.v1` flag group or entire file; rebuild and re-flash x02. Keep Phase 0 baseline immutable.

---

## Phase 2 — Font diet (x02 only)

**Goal.** Enable **only** Montserrat sizes v1 references; disable 18, 20, 26, 30, 34–48 in **`sdkconfig.defaults.v1`**. Rebuild, flash, operator validation (same script as Phase 1).

**Desk status (2026-10-08):** font block in [`firmware/sdkconfig.defaults.v1`](../firmware/sdkconfig.defaults.v1); guard [`scripts/check_v1_fonts.py`](../../scripts/check_v1_fonts.py) + `make check-v1-fonts`. Evidence: [`phase2-fonts/`](../evidence/v1-lvgl-slimming/phase2-fonts/).

### Phase 2 size (vs Phase 0 baseline and Phase 1)

| Metric | Phase 0 | Phase 1 | Phase 2 | Δ (2 vs 0) | Δ (2 vs 1) |
|--------|--------:|--------:|--------:|-----------:|-----------:|
| App bytes | 1,484,272 | 1,414,464 | **1,496,384** | +12,112 | +81,920 |
| Free bytes | 51,728 | 121,536 | **39,616** (2.6%) | −12,112 | −81,920 |
| Flash `.rodata` | 253,684 | 249,140 | **331,092** | +77,408 | +81,952 |
| `liblvgl__lvgl.a` flash | 312,310 | 245,075 | **326,933** | +14,623 | +81,858 |

Phase 0’s **built** image already linked **Montserrat 14 only** (shared `sdkconfig.defaults` intent ≠ bytes on wire). Phase 2 adds explicit **14, 16, 22, 24, 28** tables for v1 typography. **Montserrat 32** is omitted: six sizes + Phase 1 flags **overflow** the factory slot by ~6.3 KiB; `v1_auth.c` uses **28** for PIN entry when 32 is disabled.

**Operator:** re-flash and repeat the six-journey table after Phase 2 (pending).

### Locked font set (v1 source vs shipped x02)

| Size | Referenced in `firmware/v1` | Shipped in `sdkconfig.defaults.v1` |
|------|---------------------------|-------------------------------------|
| 14 | yes | yes |
| 16 | yes | yes |
| 18 | no | no |
| 20 | no | no |
| 22 | yes | yes (often not linked if 24 present) |
| 24 | yes | yes |
| 26 | no | no |
| 28 | yes | yes |
| 30 | no | no |
| 32 | yes | **no** — factory overflow ~6.3 KiB; PIN uses **28** in `v1_auth.c` |
| 34–48 | no | no |

### Implementation notes

1. Set `CONFIG_LV_FONT_MONTSERRAT_<N>=y` only for the **yes** row; set others to `n` in `sdkconfig.defaults.v1`.
2. **Do not** change `v1_*.c` font pointers unless a compile error proves a hidden dependency.
3. Add a **CI/doc guard** (optional but recommended): script `scripts/check_v1_fonts.py` that fails if `firmware/v1` references `lv_font_montserrat_<N>` where `N` is not in the locked set.
4. Keep **full ladder** in base `sdkconfig.defaults` (or `sdkconfig.defaults.island`) for **p13**.

### Build and size

Same as Phase 1: `fullclean`, x02 build, `size-components`, evidence under:

```
docs/evidence/v1-lvgl-slimming/phase2-fonts/
  summary.md           # delta vs phase0 and phase1
  x02-size-components.txt
```

Expect **`.rodata`** to grow when enabling sizes vs Phase 1 (glyph bitmaps). Versus Phase 0 baseline, net app size may still rise slightly; see [Findings](#findings--fonts-flash-and-desk-ux-2026-10-08). Record **total app bytes** and **LVGL archive** delta.

### Flash and device validation (operator)

Repeat the **same six-journey table** as Phase 1. Pay extra attention to:

- PIN pad digits and roster names (22/24/28/32).
- Carousel sender line and card meta (14).
- Record picker and send titles (16, 22, 24, 28).
- Connecting screen titles (16, 22, 24).

### Acceptance

- x02 builds with only locked fonts.
- Operator **PASS** on all six journeys.
- Document **bytes saved or spent** vs Phase 0 and Phase 1 (Phase 2 spent ~82 KiB vs Phase 1 for typography).
- `check_v1_fonts.py` / `make check-v1-fonts` passes.

---

## Phase 3 — Record decision

**Goal.** Summarize outcomes for future Opus / partition / feature work.

**Status:** `done` (2026-10-08). Full write-up: [`docs/evidence/v1-lvgl-slimming/phase3-decision/summary.md`](../evidence/v1-lvgl-slimming/phase3-decision/summary.md).

### Shipped decision

| Item | Outcome |
|------|---------|
| **Standard x02 build** | `sdkconfig.defaults` + **`sdkconfig.defaults.v1`** (Phase 1 LVGL flags + Phase 2 font ladder) |
| **Flash vs Phase 0** | **+12,112** app bytes; **−69,808** if reverting to Phase 1 fonts-only (not recommended for UX) |
| **Flash vs Phase 1** | **+81,920** for restored typography |
| **Fonts at start → now** | **1** linked (14) → **4** typical (14, 16, 24, 28); Kconfig enables 22; **32** omitted (partition) |
| **Further font slimming** | **Declined** — low ROI (~15–46 KiB per size) |
| **Plan status** | **`partial` shipped** — Phase 2 six-journey re-flash optional |

### Evidence table (committed)

See [`docs/evidence/v1-lvgl-slimming/README.md`](../evidence/v1-lvgl-slimming/README.md).

### Follow-ups (not this plan)

- Opus / codec headroom: [`x02-opus-partition` Phase 4](../evidence/x02-opus-partition/phase4-partition-strategies.md).
- IRAM **99.99%** full — unrelated to LVGL slimming.
- Optional: drop unused **22** in Kconfig; enable **32** only after ~6.3 KiB reclaimed elsewhere.

---

## Risks and mitigations

| Risk | Mitigation |
|------|------------|
| Disabled widget needed indirectly by theme/style | Incremental flag groups; rebuild after each group |
| BSP or esp-box-3 expects a decoder v1 does not call | Build fail → re-enable only that decoder |
| Shared `sdkconfig` breaks p13 | Separate `sdkconfig.defaults.v1`; separate build dirs |
| Operator skip → silent UI regression | Hard gate: no Phase 2 without Phase 1 sign-off |
| Measurement noise (different IDF point release) | Record IDF version; compare only same toolchain |

---

## Verification commands (cheat sheet)

```bash
# Phase 0/1/2 size capture (must use per-demo build dir)
cd firmware
BD=build/x02_product_shell
idf.py -D FAMILY_DEMO=x02_product_shell -B $BD fullclean build
idf.py -D FAMILY_DEMO=x02_product_shell -B $BD size-components
python3 -m esp_idf_size --archives $BD/family_link_demo.map | head -15

# Island smoke (full fonts — separate build dir)
idf.py -D FAMILY_DEMO=p13_ui_showcase -B build/p13_ui_showcase build

# Product flash (operator)
make flash DEMO=x02 WHO=lynn   # or mazi / audrey

# Timing contract unchanged by LVGL (still run before merge if timing touched)
make check-v1-parity
make check-v1-fonts
```

---

## Related documents

- [`docs/evidence/x02-opus-partition/phase1-summary.md`](../evidence/x02-opus-partition/phase1-summary.md) — historical x02 size
- [`docs/plans/v1-isolation-remaining.md`](v1-isolation-remaining.md) — do not mix carousel feature work with PIN/debug sessions
- [`docs/plans/product-00-baseline-gate.md`](product-00-baseline-gate.md) — broader product baseline
- [`docs/architecture/client.md`](../architecture/client.md) — client module map
