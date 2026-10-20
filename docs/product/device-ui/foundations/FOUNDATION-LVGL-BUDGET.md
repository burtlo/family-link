# LVGL memory and performance budget

ID: `FOUNDATION-LVGL-BUDGET`
Type: Foundation
Revision: 0.2
Status: Draft — measurement framework for v2 firmware and demos
Owner: Product owner
Approval: No firmware budget numbers are approved until measured on target
Target: v2 fixed-hardware 320 × 240 landscape canvas (RGB565, LVGL)

## Purpose and scope

This foundation defines how to **repeatably** evaluate LVGL/UI architecture and
performance for firmware demos and v2 screens. It complements visual specs under
[`docs/product/device-ui/`](../README.md) and behavioral contracts under
[`docs/product/experiences/`](../../experiences/).

It does **not** authorize a particular LVGL API (for example `lv_btnmatrix` vs
nine `lv_button` widgets). It requires each screen or demo to document choices,
budgets, and update levels so reviews stay comparable across PIN pads, carousels,
sketches, and transitions.

Related portable concepts (with v1 examples):

- [UI update taxonomy](../../../v1-assessments/ui-update-taxonomy.md) — Levels 0 / 1 / 2
- [Resource budget envelope](../../../v1-assessments/resource-budget-envelope.md) — envelope shape
- [Client application coding standards](../../../standards/client-application-coding-standards.md) §§5, 8

Canvas and touch tokens: [`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md).
Stage motion cost: [`FOUNDATION-MOTION`](FOUNDATION-MOTION.md).

## Screen assessments (repeatable reviews)

For each screen, demo, or major UI refactor, copy
[`../assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md`](../assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md),
fill every section, and store the result in
[`../assessments/`](../assessments/) with a stable name (for example
`ASSESS-SCR-PIN-ENTRY.md`, `ASSESS-DEMO-h19-record.md`).

Assessments are **evidence and recommendations** until the owner promotes a
budget line into this foundation or a screen spec.

For cross-demo pattern verdicts and the owner review queue, see
[`../assessments/DEMO-PATTERN-LEDGER.md`](../assessments/DEMO-PATTERN-LEDGER.md).
For theme-grouped demo navigation and V2 flash shortlist, see
[`../assessments/DEMO-EXPLORER-INDEX.md`](../assessments/DEMO-EXPLORER-INDEX.md).
Deep-reviewed demos can still get a full `ASSESS-DEMO-*.md` when byte budgets
and on-device timing are recorded.

## Two-budget model (normative framing)

Treat device UI memory as **two ledgers**. Mixing them in one “free heap” number
hides failures.

### Ledger A — LVGL object pool (fixed at build time)

| Fact | v2 planning default | Legacy v1 firmware evidence |
|---|---|---|
| Pool size | **64 KiB** unless sdkconfig change is measured and approved | `CONFIG_LV_MEM_SIZE` = **65536** (64 KiB); pool expand **0** |
| What it pays for | `lv_obj_*` tree, styles, labels, fonts bound to widgets, small LVGL buffers, animation user data attached to objects | Same class of allocations |
| Failure mode | `lv_malloc` failure, failed screen build, white or partial UI | Documented init failure when building all screens at startup |

**Rules:**

- Count **widget estimate** and **worst-case live nodes** against Ledger A.
- Prefer Level 0 / Level 1 updates so Ledger A high-water does not spike during
  data refresh (see update taxonomy).
- Changing `CONFIG_LV_MEM_SIZE` or font glyph sets requires a new assessment row
  and owner sign-off.

### Ledger B — Pixel, audio, and large transient buffers (heap_caps / PSRAM)

| Fact | v2 planning default | Legacy v1 firmware evidence |
|---|---|---|
| Display partial draw buffer | Size per BSP/LVGL port; record **width × draw_buf_height × 2** (RGB565) | `BSP_LCD_DRAW_BUF_HEIGHT` = **100** → **320 × 100 × 2 ≈ 62.5 KiB** per buffer; double-buffer flag off in archive build |
| Sketch / image RAM | Declare width × height × bytes-per-pixel; prefer **PSRAM** with internal fallback | Per-card sketch framebuffers allocated on demand |
| Audio / codec / download | Fixed caps (for example playback buffer **320 KiB** class in v1) | Large `heap_caps_malloc(..., MALLOC_CAP_SPIRAM)` paths |

**Rules:**

- **Pixel RAM** for canvases and `lv_image` sources is Ledger B even when LVGL
  holds a pointer to the buffer.
- Record **largest single allocation** and **largest free block** (not only total
  free bytes).
- Peak often occurs when **old and new** scene resources coexist during Level 2
  transitions — budget that overlap explicitly.

```text
Total pressure ≈ Ledger A high-water
               + Ledger B persistent (session)
               + Ledger B transient (transition / decode peak)
               + draw buffer(s) owned by display driver
```

## Standard measurement table

Copy this table into every screen assessment. Fill **Target** before implementation;
fill **Measured** on device or approved simulator.

| Column | What to record |
|---|---|
| **Scope** | Screen ID (`SCR-*`), demo ID (`h**`), or module (`PANEL-*` / `COMP-*`) |
| **Stage / snapshot** | Session stage and immutable snapshot ID (for example focused user, message seq) |
| **Update level** | 0 / 1 / 2 per [UI update taxonomy](../../../v1-assessments/ui-update-taxonomy.md) |
| **Widgets est.** | Count of LVGL objects at steady state; note pattern (9× button vs 1× btnmatrix, card count, labels per card) |
| **Widgets peak** | Maximum during transition (old + new tree overlap) |
| **Ledger A — LVGL pool** | `lv_mem_monitor`: used %, free bytes, biggest free block; or custom pool watermark |
| **Ledger B — pixel RAM** | Each buffer: W×H×bpp, caps (SPIRAM/internal), lifetime (persistent / per-card / per-frame) |
| **Ledger B — other** | Audio, download, worker stacks (words × 4), queue depth |
| **Invalidate strategy** | What marks dirty: full screen, subtree, `lv_image` only, scroll container |
| **Paint lifecycle** | Who calls paint: once per stage, on data event, on timer; idempotent for same snapshot? |
| **Anim layer cost** | Active `lv_anim` count; transform layers; cover-down full-screen overlay duration (**400 ms** per [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md)) |
| **Scroll / snap** | `LV_EVENT_SCROLL`, snap anim cancel/restart rules; refresh during scroll? |
| **Hotspot notes** | Blocking work on UI task, sync HTTP, full `lv_obj_clean`, eager N-card build |
| **Frame / build time** | p95/p99 ms for paint path and worst interaction (not only average) |
| **Degradation** | What happens at budget boundary (virtualize, drop sketch, reject) — must not white-screen or UAF |
| **Rating** | Use / Avoid / Neutral per major choice (see template) |

### RGB565 quick reference (320 × 240 canvas)

| Surface | Bytes (RGB565) |
|---|---|
| Full framebuffer | 320 × 240 × 2 = **153 600** |
| Logical sketch **120 × 120** (SK-002) | **28 800** |
| Record display **240 × 240** (SK-004) | **115 200** |
| Card outer ~**132 × 134** | ~**35 376** |

Owner decisions **SK-002–SK-004** live in
[`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md).

## Architecture review dimensions

Use these lenses in every assessment; they map to common demo comparisons.

### Widget pattern (example: PIN keypad)

| Pattern | Ledger A tradeoff | Touch / UX | Review focus |
|---|---|---|---|
| **N × `lv_button` + label** | ~2N objects; simple per-key styles and press states | Matches [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md) geometry literally | Object count, build time, event routing |
| **`lv_buttonmatrix`** | Fewer objects; shared map string; matrix styling | Must verify min touch height (`fl-touch-min-height`) | Map maintenance, long-press, disabled keys |
| **Custom draw / grid hit-test** | Lowest object count; higher code complexity | Full control | Invalidation on press feedback |

Legacy v1 product PIN paint used **nine separate buttons** (not btnmatrix). v2
should record the chosen pattern in the assessment; neither is default-approved
without measurement.

### Update level default policy (v2)

| Situation | Default level |
|---|---|
| Timer, progress, connectivity dots, transport, PIN dots | **0** |
| Inbox diff, roster card add/remove, single panel swap | **1** (stable IDs) |
| Roster ↔ PIN ↔ carousel ↔ record ↔ receipt | **2** |
| Inbox WebSocket refresh while staying on carousel | **1**, not **2** |

### Scene paint lifecycle (v2)

| Policy | v2 default | Legacy v1 tendency |
|---|---|---|
| Level 2 entry | Cancel anims/timers → **invalidate all module handles** → build from snapshot | Partially followed; carousel UAF class drove `invalidate_ui` helpers |
| Level 2 paint | Prefer **persistent shell** ([`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)) + swap `PANEL-*` content | Often `lv_obj_clean(scr)` on full screen including ribbons |
| Level 1 paint | Diff collection; preserve scroll/focus by ID | `paint_carousel` + `lv_obj_clean` after inbox reload |
| Re-enter same stage | Idempotent paint from snapshot | Sometimes relies on surviving widgets |

### Sketch buffers (v2 vs v1)

| Topic | v2 owner policy (SK-002–004) | Legacy v1 as-built |
|---|---|---|
| Stored coordinates | **120 × 120** logical, 1:1 on card | Full-screen record; rescale on card replay |
| Record UI buffer | **240 × 240** display → map ÷2 to logical | **320 × 240** paint |
| Playback | **120 × 120** inside ~132×134 card chrome | ~132×134 card with scaled points |
| Color | Per-stroke color (format TBD) | Single ink color per message |

## Grep / query cookbook

Run against the **active firmware tree** (future v2 app root) or a named demo
directory. Use results to fill the measurement table and hotspot notes.

### Scene destruction and leaks

| Search | Intent |
|---|---|
| `lv_obj_clean` | Full-screen teardown; pair with update level — is it justified? |
| `lv_obj_del` / `lv_obj_delete` | Targeted removal vs clean |
| `invalidate_ui` / `invalidate_*` | Cross-module handle clearing before another scene destroys tree |
| `lv_anim_del` / `lv_timer_del` | Lifecycle cleanup before Level 2 |

### Images, canvas, pixel RAM

| Search | Intent |
|---|---|
| `lv_image` / `lv_img` | Widget-backed images; buffer source and size |
| `lv_canvas` | Drawing surface; buffer allocation site |
| `heap_caps_malloc` | Ledger B; note `MALLOC_CAP_SPIRAM` vs internal |
| `MALLOC_CAP_SPIRAM` | PSRAM policy and fallback paths |
| `RGB565` / `LV_COLOR_FORMAT` | Format consistency with theme and sketch |

### Input, scroll, refresh storms

| Search | Intent |
|---|---|
| `LV_EVENT_SCROLL` | Work done per scroll tick |
| `SCROLL_END` | Snap logic; guard against recursive snap (scroll lock pattern) |
| `lv_obj_invalidate` | Granularity of redraw |
| `lv_refr_now` | Forced full refresh — avoid in hot paths |

### Animation and transitions

| Search | Intent |
|---|---|
| `lv_anim` | Count concurrent anims; cover-down overlays |
| `lv_obj_set_style_transform` | Transform layer cost on carousel cards |
| `ease` / `path_cb` | Motion from [`FOUNDATION-MOTION`](FOUNDATION-MOTION.md) |

### Keypad and grids

| Search | Intent |
|---|---|
| `lv_buttonmatrix` / `btnmatrix` | Matrix keypad implementations |
| `lv_button_create` | Per-key buttons (v1 PIN style) |
| `LV_EVENT_CLICKED` + pin / key | Event path to auth worker |

### Observability already in legacy code

| Search | Intent |
|---|---|
| `lv_mem_monitor` | Ledger A snapshot |
| `esp_get_free_heap_size` | Ambiguous — split by caps when possible |
| `heap_caps_get_free_size` | Ledger B by capability |
| `paint.*heap` / `ESP_LOGI.*heap` | Before/after paint logging patterns |

### Blocking work (standards violations)

| Search | Intent |
|---|---|
| `esp_http_client` / `http` in `ui` / `paint` | UI-thread network |
| `vTaskDelay` in paint or event cb | Blocking UI |
| `sync_.*scroll` / `on_.*click` + `session/view` | Gesture-coupled remote I/O |

## V1 → v2 default policy deltas

These are **v2 planning defaults** unless a screen spec or assessment promotes
something else with measurement.

1. **Inbox and roster data refresh** → Level **1** diff; avoid carousel-wide
   `lv_obj_clean` for “new messages arrived.”
2. **Signed-in shell** → persistent [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md)
   chrome; swap panels instead of wiping ribbons + content on every sub-state.
3. **Sketch geometry** → **120×120** logical storage, **240×240** record display,
   **1:1** card replay ([`EXP-SKETCH-RECORD-PLAYBACK`](../../experiences/sketch-record-and-playback.md) SK-002–004).
4. **Carousel presentation** → virtualize live cards (center + neighbors); bound
   simultaneous sketch framebuffers.
5. **Typography / fonts** → only **`fl-type-16` / `24` / `32`** and weight **600**
   ([`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md)); measure glyph flash impact.
6. **Login stage motion** → **cover-down** default; cross-fade withdrawn for roster ↔ PIN
   ([`FOUNDATION-MOTION`](FOUNDATION-MOTION.md)).
7. **Handle lifecycle** → mandatory module invalidation before any foreign Level 2
   destroys the tree (standards §5).
8. **Resource claims** → declared per-scene **budget envelope** with degradation,
   not heap logging only ([resource budget envelope](../../../v1-assessments/resource-budget-envelope.md)).
9. **UI task** → no blocking I/O or worker shutdown spins in paint/input paths.
10. **PIN UI** → geometry from [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md);
    widget pattern (9× button vs btnmatrix) decided per assessment.

## Guided re-implementation and heap pressure

Re-building v1 behavior **using**
[`docs/v1-assessments/`](../../../v1-assessments/) and
[`client application coding standards`](../../../standards/client-application-coding-standards.md)
**partially** relieves heap pressure — **only if** those guides change what ships,
not merely how it is documented.

| Outcome | Condition |
|---|---|
| **Ledger A (64 KiB LVGL)** | **Yes, material** when inbox/roster use **Level 1** diff, send receipt uses **Level 0/1**, carousel **virtualizes** live cards, and **Level 2** is reserved for true stage changes ([`ui-update-taxonomy`](../../../v1-assessments/ui-update-taxonomy.md), [`carousel-playback`](../../../v1-assessments/carousel-playback.md)). |
| **Ledger B (pixels / media)** | **Yes, material** when sketch/record follow SK-002–004 sizes, sketch buffers are **bounded**, playback/upload **stream** without duplicate full-message peaks ([`recording-send`](../../../v1-assessments/recording-send.md), [`resource-budget-envelope`](../../../v1-assessments/resource-budget-envelope.md)). |
| **System / internal heap** | **Indirect** — eases when large allocations stay on PSRAM with explicit margins; guides do not remove fixed **~62.5 KiB** draw-buffer cost or playback-buffer class unless product/sdkconfig changes. |

A rewrite that **copies** v1 patterns (full `paint_carousel` clean, eager max cards,
320×240 record FB, nine PIN buttons without measurement) can **match** v1 heap
behavior despite reading the assessments. Treat per-scene
[`ResourceBudget`](../../../v1-assessments/resource-budget-envelope.md) and filled
`ASSESS-*.md` rows as **gates**, not optional commentary.

## Workflow for agents and implementers

1. Read experience + screen spec + this foundation.
2. Copy the [assessment template](../assessments/TEMPLATE-LVGL-SCREEN-ASSESSMENT.md).
3. Classify each user-visible transition with update level **before** choosing APIs.
4. Fill the measurement table **Target** column from geometry and widget estimates.
5. Implement or review demo code; run grep cookbook; capture **Measured** on device.
6. File assessment under `assessments/`; link from screen spec **Implementation
   evidence** when available.

Do not treat legacy v1 firmware as mandatory architecture — extract numbers and
anti-patterns, then justify v2 choices in the assessment.

## Appendix A — Optional on-device probes (non-normative)

Use when a lab build is available. Probes supplement; they do not replace the
measurement table.

### LVGL pool snapshot

```c
lv_mem_monitor_t mon;
lv_mem_monitor(&mon);
/* Log: mon.total_size, mon.free_size, mon.free_biggest_size,
        mon.used_pct, mon.max_used */
```

Log once at steady state and once immediately after worst-case Level 2 paint.

### Before / after paint

```c
/* Ledger B example */
size_t free_psram = heap_caps_get_free_size(MALLOC_CAP_SPIRAM);
size_t largest_psram = heap_caps_get_largest_free_block(MALLOC_CAP_SPIRAM);
/* paint_fn(); */
/* Log delta and mon.max_used */
```

### Frame timing

Wrap the UI task paint entry or `lv_timer_handler` duration; record p95/p99 over
30–60 s of scripted interaction (scroll, snap, PIN entry, sketch stroke).

### Scripted scenarios (minimum set)

| Scenario | Pass hint |
|---|---|
| Cold boot → roster | Ledger A < pool limit; no failed `lv_*_create` |
| Roster → PIN → roster | No stale handle crash; peak widgets documented |
| Carousel scroll + snap | No multi-second stall; scroll lock prevents snap loop |
| Inbox refresh while on carousel | Level 1; scroll position preserved |
| Record / sketch stroke | Ledger B buffer stable; Level 0 invalidates image only |

## Revision history

| Rev | Date | Change |
|---|---|---|
| 0.1 | 2026-10-11 | Initial LVGL budget foundation and assessment workflow |
| 0.2 | 2026-10-11 | Guided re-implementation vs heap pressure (v1-assessments + standards) |
