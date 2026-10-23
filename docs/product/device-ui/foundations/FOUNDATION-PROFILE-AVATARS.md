# Profile avatar catalog and assets

ID: `FOUNDATION-PROFILE-AVATARS`
Type: Foundation
Revision: 0.4
Status: Draft — avatar artwork deferred; LAB-14 initials fallback approved for now
Owner: Product owner
Approval: No visual approval claimed for catalog artwork

## Owner intent (recorded)

- Each hangout user has a **profile avatar** chosen from a **fixed catalog** (not
  free-form uploads on the device).
- The choice is **server-owned profile** (`avatar_id`), synchronized like accent
  and card background
  ([user profile](../../communication-contracts/user-profile.md) `COM-006`).
- Avatars are **always shown** on user identity surfaces (roster cards, PIN
  header, future settings picker). See [`COMP-USER-CARD`](../components/COMP-USER-CARD.md).
- Avatar **pixel colors are fixed per catalog entry**. They are **not** tinted by
  the user's `profile_accent`, which core V2 cards do not render.
- Until the owner-authored V2 bitmap catalog is ready, identity surfaces use the
  LAB-14 **initials in a centered circle** treatment. This is a temporary visual
  fallback, not an avatar ID or a decision to carry V1 avatar art forward.

## Catalog identity (proposed)

| Rule | Value |
|---|---|
| ID pattern | TBD with the owner-authored V2 catalog; current `avatar-NN` examples are provisional |
| Count and art | TBD with the owner-authored V2 catalog; do not promote V1 avatar art |
| Server validation | Final allowed IDs and default remain in `COM-OPEN-003` in the profile contract |
| V1 migration | Deferred; do not assume a V1 `avatar_slot` maps to a V2 avatar |

Individual character designs, host PNG files, and flash image headers are **not**
approved until listed in a future catalog revision table.

## Display sizes (on device)

The same flash asset is **scaled** per surface. Tokens live in
[`catalog/tokens.yaml`](../catalog/tokens.yaml).

| Surface | Token | Display size | Authority |
|---|---|---|---|
| User card (roster, inbox, send picker) | `fl-avatar-roster` / `user_card.avatar` | **66 px** | [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) |
| PIN identity row | `pin_status.avatar` | **28 px** | [`PANEL-HEADER-PIN-STATUS`](../panels/PANEL-HEADER-PIN-STATUS.md) |
| Settings face picker (future) | *(TBD panel spec)* | **~48 px** face in **~54 px** tap target | Legacy v1 evidence; panel not authored |

Design art so identity survives **28 px** display (simple silhouette, high
contrast). Keep the character inside a **~60 px diameter** safe circle centered on
the master canvas.

## Authoring format (host)

| Field | Rule |
|---|---|
| Master raster | **66 × 66 px** square PNG (preferred). Optional **48 × 48** export only if flash budget requires the smaller source. |
| Color space | sRGB while editing; ship **RGB565** on device |
| Background | Solid matte **`#101418`** (`fl-color-surface`) filling the square — matches dark cards; do not rely on alpha |
| Transparency | **Not** used in the v2 pipeline (`LV_COLOR_FORMAT_RGB565`); bake background in the art |
| Quantization | Host MUST pack with RGB565 5-6-5: `((R & 0xF8) << 8) \| ((G & 0xFC) << 3) \| (B >> 3)`, little-endian bytes in firmware arrays |
| Gradients | Minimize soft gradients (band on RGB565). Prefer flat fills and ≤ ~6 colors per avatar plus background |
| Line weight | No 1 px critical detail ([`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md)) |

### Off-device workflow (normative)

1. Author PNGs on a host (`avatar-01.png` …).
2. Convert offline to **`lv_image_dsc_t` + `uint8_t[]`** in flash (same approach as legacy persona portrait pack and creature generators).
3. Map `avatar_id` → descriptor in firmware; render with **`lv_image`** ([`ASSESS-DEMO-p10`](../assessments/ASSESS-DEMO-p10.md)).

The device **MUST NOT** depend on PNG, JPEG, or zlib at runtime for catalog
avatars. Optional PNG copies for browser or server admin twins are host-only.

### Quality gate (before flash)

- Downscale master to **28 × 28** (nearest-neighbor) and confirm recognition.
- Row of **N** thumbnails at roster size: silhouettes must differ at a glance
  (up to **8** users visible in roster).

## Product constraints

- Original, family-safe character art only.
- **No** child photographs as avatars without explicit owner consent.
- **No** licensed or trademarked characters.
- **No** text or numerals inside the bitmap.

## Rendering and performance

| Topic | Rule |
|---|---|
| Storage | Pixel data in **flash** (`const` RGB565). Not heap-resident catalog storage. |
| Per visible portrait | One `lv_image` (Ledger A object pool). See [`FOUNDATION-LVGL-BUDGET`](FOUNDATION-LVGL-BUDGET.md). |
| Decode | **None** at runtime when pre-packed RGB565 |
| Draw | LVGL blits on dirty regions; application code does not repaint pixels each frame |
| Flash budget (planning) | 66×66×2 ≈ **8.7 KiB** per entry; 48×48×2 ≈ **4.5 KiB**; total depends on the owner-selected catalog count |
| RAM cache | Do **not** duplicate full RGB565 into RAM unless profiling shows benefit; avoid per-user decoded buffers |
| Circular mask | Cards conceptually show a **circle**; applying a circular clip over solid card fills may still cost an extra compositing layer. Keep the LAB-14 circular initials fallback while the owner reviews bitmap clipping (`UI-OPEN-002`). |
| Interim fallback | Use the `initials_from_name` behavior in LAB-14: first character, plus the character after the first space when present. Center it in the circle. Roster/message cards use a 66 px `#F4F4F4` face and dark `fl-type-16` initials; the PIN identity row uses a 28 px `#5AA0E8` face and dark initials. A missing or unrecognized `avatar_id` uses this treatment, and that person stays selectable (`COM-010` in the profile contract). This is temporary until owner-authored artwork is ready. |

Roster avatar changes are **Level 1** updates ([`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md)): swap image source or portrait widget, not a full-screen framebuffer.

## Dependencies

- [`FOUNDATION-DEVICE-CANVAS`](FOUNDATION-DEVICE-CANVAS.md) — canvas, RGB565, touch
- [`THEME-DARK`](../themes/THEME-DARK.md) — surface color for matte background
- [`user-profile`](../../communication-contracts/user-profile.md) — `avatar_id` field

## OPEN items

| ID | Question | Blocks |
|---|---|---|
| `UI-OPEN-001` | Final server default `avatar_id` and migration behavior for V1 `avatar_slot` | Profile migration; device display fallback is decided as initials |
| `UI-OPEN-002` | Circular clip on 66 px roster portraits vs square bitmap (performance vs spec circle) | Carousel/card implementation |
| `UI-OPEN-003` | Author and approve the owner-selected art entries and manifest for server/browser twins | Visual approval, `COM-OPEN-003` closure for avatars |

## Readiness

- **Owner review:** Asset dimensions, RGB565 pipeline, separation from accent, and
  family-safety rules are recorded. New artwork is deferred; LAB-14 centered
  initials are the approved interim fallback.
- **Visual approval:** Pending finished catalog art (`UI-OPEN-003`).
- **Implementation:** Firmware MAY implement lookup + `lv_image` scale using these
  rules before art is final. While the new catalog is pending, identity surfaces
  use centered LAB-14 initials in a circle.

## Revision history

- **0.4 — 2026-10-22:** A missing or unrecognized `avatar_id` uses the initials fallback, and that person stays selectable.
- **0.3 — 2026-10-22:** Clarified that avatar colors are independent of `profile_accent`, which is not rendered on core V2 cards.
- **0.2 — 2026-10-22:** Owner deferred avatar artwork and rejected carrying the V1
  art forward for now; adopted LAB-14 centered initials as the interim fallback.
- **0.1 — 2026-10-22:** Initial catalog rules, sizes, RGB565 authoring, host pack
  pipeline, and performance framing from v1 evidence and v2 tokens.
