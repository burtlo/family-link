# Demo pattern ledger — LVGL USE vs AVOID (V2)

Status: working review artifact
Revision: **0.2**
Owner: Product / firmware agents
Evidence: full **h** / **p** / **x** island catalog (static scan of legacy v1 POC firmware demos) + product shell **x02** modules (connect, auth, carousel, record)
Normative V2 UI: [`device-ui/README.md`](../README.md)
Measurement worksheet: [`TEMPLATE-LVGL-SCREEN-ASSESSMENT.md`](TEMPLATE-LVGL-SCREEN-ASSESSMENT.md) · [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md)
Thematic catalog (purpose, PASS, flash shortlist): [`DEMO-EXPLORER-INDEX`](DEMO-EXPLORER-INDEX.md)

This ledger catalogs **as-built LVGL patterns** from the v1 POC archive and assigns a
V2 verdict. It does **not** authorize implementation by itself; paired screen specs
(`SCR-*`), components (`COMP-*`), and experiences (`EXP-*`) remain authoritative.

**Review depth:** **DEEP** rows follow the full column contract and prior module
analysis. **SCAN** rows are static code review (grep + header intent) — fill
`ASSESS-DEMO-*.md` before promoting budgets.

## Column contract (TEMPLATE-LVGL-SCREEN-ASSESSMENT compatible)

| Column | Meaning |
|---|---|
| **ID** | Demo id (`h19`) or shell module (`v1_carousel`) |
| **Depth** | `DEEP` or `SCAN` |
| **Purpose** | What the artifact proved |
| **Tree model** | Persistent root vs `lv_obj_clean` repaint; scroll containers; animation style |
| **Canvas / FB** | RGB565 framebuffer or canvas sizes when allocated |
| **Verdict** | **ADOPT** · **ADAPT** · **AVOID** · **SKIP** for V2 |
| **Rationale** | One line |
| **V2 specs** | Linked product UI artifacts and experiences |

---

## Catalog (deep-reviewed)

| ID | Depth | Purpose | Tree model | Canvas / FB | Verdict | Rationale | V2 specs |
|---|---|---|---|---|---|---|---|
| **h03** | DEEP | GT911 PIN pad without network or message body | Single **persistent** screen; keys created once in `paint_pad`; no scroll | None (vector buttons only) | **ADAPT** | Key grid math (98×42, 6 px gap, 320×240) informs keypad touch targets; v2 drops Clear/0 row per policy | [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md), [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md), [`EXP-LOGIN`](../../experiences/endpoint-login.md) |
| **h15** | DEEP | PIN then fetch inbox; locked state shows **N new** only | **Level 2**: `lv_obj_clean(scr)` on pad paint and again replacing entire screen with body text | None | **ADAPT** | Privacy badge on pad is good; full-screen clean to show one label is acceptable only as stage change, not data refresh | [`EXP-LOGIN`](../../experiences/endpoint-login.md), [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md) |
| **h18** | DEEP | Playback screen: metadata, bar, ROOMVOL, streaming audio | **Mixed**: `lv_obj_clean` when loading a new catalog message; **Level 0** `ui_refresh()` for bar/clock/play label during play | None | **ADOPT** | In-stage transport updates without rebuild; rebuild only when message identity changes | (Inbox playback TBD); see [`ui-update-taxonomy`](../../../v1-assessments/ui-update-taxonomy.md) |
| **h19** | DEEP | Inbox list ↔ detail transitions; record-entry motion stubs | **Persistent root** (`s_root_scr`) with clip/wrap hosts; list **vertical scroll**; stage change via **transform anims** (400 ms ease-in-out quint), not screen delete | Record UI is **stub**: 240×240 **styled panel** + 80 px palette rail — **no** RGB565 record FB | **ADOPT** | Layered persistent tree + cover-down/up is the V2 default stage motion | [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md), [`ASSESS-DEMO-h19`](ASSESS-DEMO-h19.md), [`EXP-LOGIN`](../../experiences/endpoint-login.md) |
| **h27** | DEEP | Store-and-forward drawing note (two-kit protocol) | **Level 2** `lv_obj_clean` per stage (home / record / play) | **320×240×2** RGB565 (`FB_BYTES`) in SPIRAM/internal; point stream up to 2048 pts | **ADAPT** | Full-canvas FB matches timed sketch replay; V2 should budget FB lifetime and prefer spec’d drawing layout over ad-hoc buttons | [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) |
| **p04** | DEEP | Mood face **property tweens** (~300 ms) vs hard cuts | **Persistent** face subtree; `lv_anim` on x/y/height only; caption text swap | None | **ADOPT** | Model for ribbon/mascot **Level 0** motion without scene replacement | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |
| **p13** | DEEP | LVGL lab: scroll modes, snap carousel, modals, canvas rows | Menu + **per-page screens**; horizontal scroll **snap** pages; optional **lv_canvas** row buffers (`TRI_ROW_W×H`, snap card W×H) | Per-widget canvas buffers on some scroll pages (tens of KB, not full screen) | **ADOPT** | Reference for snap math, scrollbar modes, and chrome/content split — not a product screen | [`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md), [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) |
| **x02** | DEEP | Stage router: connecting → roster → PIN → carousel → record → send | **Level 2** dispatch: one `lv_obj_clean(scr)` per `paint_*` entry from `ui_task` | N/A (delegates to modules) | **ADAPT** | Central stage table is right; unconditional repaint flags should not re-clean unchanged stages | [`PANEL-MODULE-CONTRACT`](../patterns/PANEL-MODULE-CONTRACT.md), [`session-stage-vs-background-poller`](../../../v1-assessments/session-stage-vs-background-poller.md) |
| **v1_connect** | DEEP | Server probe UI, Wi-Fi error, **sign-in roster** horizontal snap | Connecting: clean once; **dots refresh in place** (comment: avoid clean flash). Roster: clean + horizontal scroll, snap anim shared with carousel timing constants | Portrait paint into cards; no full-screen FB | **ADOPT** | Roster carousel UX and non-destructive connecting-dot updates align with `PANEL-BODY-ROSTER-CAROUSEL` | [`SCR-SIGN-IN-ROSTER`](../screens/SCR-SIGN-IN-ROSTER.md), [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md), [`EXP-LOGIN`](../../experiences/endpoint-login.md) |
| **v1_auth** | DEEP | PIN stage with ribbons + header cluster + lower keypad band | **Level 2** `lv_obj_clean(scr)` each paint; keypad grid uses `V1_SCROLL_CARD_Y/H` band (same as carousel cards) | None | **ADOPT** | Split header + lower keypad geometry is the basis for [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) + [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md) | [`SCR-PIN-ENTRY`](../screens/SCR-PIN-ENTRY.md), [`SCR-PIN-COOLDOWN`](../screens/SCR-PIN-COOLDOWN.md) |
| **v1_carousel** | DEEP | Inbox carousel, settings cards, transport, sketch on card | Horizontal **scroll** + snap (`s_scroll_lock`, `snap_scroll_to`); **opacity** focus from scroll; **paint_carousel** = `lv_obj_clean` + rebuild all cards | Per-card sketch slots `132×(content−header)`; up to `V1_MSG_MAX` cards materialized | **AVOID** (patterns) | Correct snap/transport refresh ideas buried under full repaints, scroll-time portrait rebuild, sync HTTP on gestures | [`carousel-playback`](../../../v1-assessments/carousel-playback.md), [`ui-update-taxonomy`](../../../v1-assessments/ui-update-taxonomy.md), `COMP-USER-CARD` |
| **v1_record** | DEEP | Recipient picker snap, record canvas, send receipt | **Level 2** clean on picker/record/send paints | **320×240×2** record FB (`RECORD_FB_BYTES`); brush stroke raster | **ADAPT** | Full-screen capture FB is proven but heavy; V2 should match experience layout and separate capture from send receipt **Level 0** updates | [`recording-send`](../../../v1-assessments/recording-send.md), [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) |

---

## Catalog (scan-reviewed) — island demos **h**

| ID | Depth | Purpose | Tree model | Canvas / FB | Verdict | Rationale | V2 specs |
|---|---|---|---|---|---|---|---|
| **h02** | SCAN | Locked-idle **N new** + PWM dim smoke | One-shot labels on active screen; no teardown | None | **ADOPT** | Typography/layout reference for empty hangout / badge count | [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md) |
| **h04** | SCAN | Mute key is **hold-to-talk** edges, not toggle | Single `lv_label` **Level 0** text updates | None | **ADAPT** | Hardware PTT proof; LVGL is diagnostic only | — |
| **h05** | SCAN | Mic loopback hold/release | `board_status` text only (no LVGL tree) | ~640 B–few KiB PCM | **SKIP** | Audio path qualification | — |
| **h06** | SCAN | Wi-Fi STA join hold 60 s | No display | None | **SKIP** | Network only | — |
| **h07** | SCAN | HTTP `GET /v1/me` bearer | `board_status` only | None | **SKIP** | API smoke | [`EXP-LOGIN`](../../experiences/endpoint-login.md) (wire only) |
| **h08** | SCAN | Hold-to-record WAV upload | `board_status` only | **~320 KiB** PCM cap (10 s) | **SKIP** | Upload protocol; not UI architecture | [`recording-send`](../../../v1-assessments/recording-send.md) |
| **h09** | SCAN | Download inbox audio + play | `board_status` only | Multi‑KiB download blob | **SKIP** | Media path | — |
| **h10** | SCAN | Playhead survives reboot | `board_status` only | None | **SKIP** | Server state contract | — |
| **h11** | SCAN | Hangout PTT relay (no LCD requirement) | No LVGL | None | **SKIP** | Audio / WS | — |
| **h12** | SCAN | h11 + LCD refresh decoupled from PCM | `board_status` on **500 ms** UI task | None | **ADAPT** | Proves paint rate cap for live hangout chrome (not per audio frame) | — |
| **h13** | SCAN | Show packed RGB565 photo full screen | `lv_obj_clean` + `lv_image` | **320×240×2** (~153 KiB) | **ADAPT** | Full-screen blit path; v2 portraits should use smaller assets + card slots | [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) |
| **h14** | SCAN | Heartbeat + WS inbox badge | `lv_obj_clean` once; badge/peer **Level 0** | None | **ADAPT** | Good locked-idle refresh; initial clean OK as bootstrap only | [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md) |
| **h16** | SCAN | HTTPS `GET /v1/me` | `board_status` only | None | **SKIP** | TLS smoke (duplicate of h07) | — |
| **h17** | SCAN | Button panel + mute GPIO + chirps | Persistent rows; **Level 0** value labels | None | **ADOPT** | Model for hardware diagnostics / dev panel pattern | — |
| **h20** | SCAN | Mute latch → open/away presence | `lv_obj_clean` once; card **Level 0** | None | **ADOPT** | Two-card layout + in-place state colors | — |
| **h21** | SCAN | Open-line talk + room volume slider | `lv_obj_clean` once; **Level 0** refresh | None | **ADOPT** | Slider + status without scene rebuild | — |
| **h22** | SCAN | Diary chunked WAV upload | `board_status` only | ~64 KiB chunk buffer | **SKIP** | Protocol / mute gating | — |
| **h23** | SCAN | Tap record + idle stop upload | `board_status` only | **~9.6 MiB** PCM cap (300 s) | **SKIP** | Record policy lab; **Ledger B** risk if copied | [`recording-send`](../../../v1-assessments/recording-send.md) |
| **h24** | SCAN | Device log batch upload | `board_status` only | None | **SKIP** | Telemetry | — |
| **h25** | SCAN | Chipmunk playback effect | `board_status` only | Audio alloc | **SKIP** | FX experiment | — |
| **h26** | SCAN | Real-time shared drawing (WS) | `lv_image` + FB; `lv_obj_clean` on clear | **320×240×2** persistent | **ADAPT** | Proves collaborative ink; not store-and-forward sketch format | [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) |
| **h28** | SCAN | Short animated clip CPU-painted | Timer blit to `lv_image` | **320×240×2** | **ADAPT** | FPS / SPI stress — product is not video | — |
| **h29** | SCAN | Async voice ping-pong | Home `lv_obj_clean` once; status **Level 0** | **~320 KiB** PCM blob | **ADAPT** | UX for hold-to-send; bound audio RAM in v2 | — |
| **h30** | SCAN | Local Opus soak 3 min | `board_status` only | **~600 KiB** enc store | **SKIP** | Codec soak | — |
| **h31** | SCAN | Opus chunk upload + range play | `board_status` only | 48 KiB chunk + 16 KiB play | **SKIP** | Streaming protocol | — |
| **h32** | SCAN | On-chip FAT qualification | No LVGL | None | **SKIP** | Storage Stage A | — |
| **h35** | SCAN | SDMMC discovery | No LVGL | None | **SKIP** | Storage Stage A | — |
| **h37** | SCAN | SDMMC metadata read | No LVGL | None | **SKIP** | Storage Stage A | — |
| **h38** | SCAN | SDMMC FAT filesystem I/O | No LVGL | None | **SKIP** | Storage Stage A | — |

## Catalog (scan-reviewed) — island demos **p**

| ID | Depth | Purpose | Tree model | Canvas / FB | Verdict | Rationale | V2 specs |
|---|---|---|---|---|---|---|---|
| **p01** | SCAN | Static geometric mascot | ~15 `lv_obj` shapes; one paint | None | **ADAPT** | Widget-count baseline for ribbon face | [`FOUNDATION-DEVICE-CANVAS`](../foundations/FOUNDATION-DEVICE-CANVAS.md) |
| **p02** | SCAN | Blink + breathe idle | **Level 0** anims on existing parts | None | **ADOPT** | Idle motion + FPS logging pattern | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |
| **p03** | SCAN | Mood **hard cuts** via mute cycle | Rebuild face layout per mood | None | **AVOID** | Prefer p04 / foundation motion defaults | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |
| **p05** | SCAN | Press / listen / poke react | Property updates on persistent face | None | **ADOPT** | Input feedback without Level 2 | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |
| **p06** | SCAN | ES8311 chirp suite | Minimal or no LVGL | None | **SKIP** | Audio-only | — |
| **p07** | SCAN | Pet loop blink + mute face | Timer **Level 0** | None | **ADOPT** | Long-run idle stability | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |
| **p08** | SCAN | Inbox notice badge + chirp | Badge/caption **Level 0** | None | **ADOPT** | Unread count without message body | [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md) |
| **p09** | SCAN | Mouth flap during tone | Widget mouth + audio | Small PCM | **ADAPT** | Freeze-face vs flap product policy | — |
| **p10** | SCAN | Packed persona `lv_image` | Static image + caption | Flash-backed RGB565 asset | **ADOPT** | Portrait pipeline reference | [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) |
| **p11** | SCAN | Greeting WAV replay | Portrait persists | None | **SKIP** | Audio personality | — |
| **p12** | SCAN | Tamagotchi in **CPU FB** | Blit to buffer → single `lv_image` | **320×240×2** | **ADAPT** | Avoids SPI tear; prefer bounded regions in v2 | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) |

## Catalog (scan-reviewed) — island demos **x**

| ID | Depth | Purpose | Tree model | Canvas / FB | Verdict | Rationale | V2 specs |
|---|---|---|---|---|---|---|---|
| **x01** | SCAN | Pre-x02 monolithic shell (h02–h12 wiring) | **Level 2** `lv_obj_clean` per app state | **320×240×2** preview + **320 KiB** session buf | **AVOID** | Superseded by x02 module split; heavy all-in-one RAM | — |
| **x02-opus-dep** | SCAN | x02 + Opus link probe (negative control) | Delegates to x02 main | None | **SKIP** | Build / size qualification | — |
| **x02-opus-probe** | SCAN | x02 + Opus footprint probe | Delegates to x02 main | None | **SKIP** | Build / size qualification | — |

---

## Summary (revision 0.2 scan pass)

**Island demo ids cataloged:** **51** — **34** `h**` + **13** `p**` + **4** `x**` sources (`x01`, `x02`, two Opus build probes).
**Shell module rows (DEEP):** **4** (`v1_connect`, `v1_auth`, `v1_carousel`, `v1_record`) decomposed from **x02**.
**Total ledger rows:** **55** (51 island + 4 shell modules).

### Verdict counts (all rows)

| Verdict | Count | Notes |
|---|---:|---|
| **ADOPT** | **15** | Patterns to carry into v2 specs (motion, roster, PIN shell, transport refresh, …) |
| **ADAPT** | **15** | Useful with measurement / policy changes (sketch FB, monolithic router, scan demos) |
| **AVOID** | **3** | `v1_carousel` patterns, `p03` hard cuts, `x01` monolith |
| **SKIP** | **22** | Network/audio/storage qualifications; no LVGL architecture |

### Top heap / Ledger B risks (do not copy blindly)

| Risk | Where observed | Approx. peak | Mitigation |
|---|---|---|---|
| Full-screen sketch/record FB | h27, h26, h28, p12, h13, v1_record | **153 600 B** each (+ draw buf ~62.5 KiB) | SK-002–004 sizes; single active capture surface |
| Long PCM hold in RAM | h23 | **~9.6 MiB** (`300 s` cap) | Stream or cap takes per product policy |
| Opus soak store | h30 | **~600 KiB** enc store | Lab only |
| Voice message blob | h08, h29 | **~320 KiB** (10 s) | PSRAM + explicit envelope |
| x01 session buffer | x01 | **320 KiB** + preview FB | Retired with x02 modules |
| Eager carousel cards + sketch slots | v1_carousel | N × card + sketch | Virtualize; Level 1 inbox diff |

### V2-irrelevant demos (SKIP — keep for lab, not UI specs)

**h:** h05, h06, h07, h08, h09, h10, h11, h16, h22, h23, h24, h25, h30, h31, h32, h35, h37, h38
**p:** p06, p11
**x:** x02-opus-dep, x02-opus-probe

---

## Anti-patterns (do not carry forward)

| Anti-pattern | Where observed | Why it hurts V2 | Preferred level |
|---|---|---|---|
| **Rebuild sender portrait on every horizontal scroll** | `carousel_refresh_sender_header()` calls `lv_obj_clean(s_sender_face)` then repaints portrait; invoked from `refresh_card_visuals_from_scroll()` on `LV_EVENT_SCROLL` | Allocates widgets and triangle portraits during gesture; competes with snap animation and I2S | **Level 0**: swap image source / gradient id; rebuild portrait only when focused **message id** changes |
| **Inbox/network refresh → full carousel scene** | `paint_carousel` → `lv_obj_clean(scr)` after WS inbox reload | Data refresh is not a session stage; destroys scroll handles and risks UAF if restore flags wrong | **Level 1** identity-keyed card diff ([`ui-update-taxonomy`](../../../v1-assessments/ui-update-taxonomy.md)) |
| **Synchronous HTTP in scroll / tap handlers** | `sync_focus_from_scroll`, card click view writes | Blocks `ui_task`; manifests as jank and missed frames | Enqueue coalesced view/write; local focus immediate |
| **Repaint connecting screen to animate dots** | Avoided in `v1_connect` (explicit comment) — listed as contrast | Full clean flashes white frame | **Level 0** dot visibility only |
| **Send receipt phase → full `paint_send` clean** | `v1_record` send phases | `Finishing` → `Sending...` is copy change, not new stage | **Level 0** label/progress; **Level 1** if portraits appear |
| **Eager max card count + per-card sketch FB** | `v1_carousel` materializes all messages | RAM high-water on large inboxes | Virtualize to center ± neighbors; cap decoded sketch bytes |
| **Dangling widget handles after foreign clean** | Historical carousel+PIN hybrid; mitigated by `s_carousel_ui_live` guard | Crash or mixed UI | Invalidate all module handles on any **Level 2** exit; single owner per screen |
| **h19-style record stub mistaken for capture impl** | h19 record panel is colored `lv_obj` + palette swatches only | Teams may ship non-functional draw surface | Use h27 / `v1_record` FB evidence when implementing real ink |
| **h23-scale PCM reservation** | h23 `PCM_CAP` for 300 s | Dominates PSRAM; unrelated to v2 UI | Stream upload or hard cap per [`recording-send`](../../../v1-assessments/recording-send.md) |

---

## Recommended V2 patterns

| Pattern | Source evidence | V2 home |
|---|---|---|
| **Cover-down / cover-up stage transition** (400 ms, ease-in-out quint; incoming `y: 240→0`) | h19 `H19_TRANS_COVER_DOWN`, `TRANS_MS` | [`TRANSITION-COVER-DOWN`](../patterns/TRANSITION-COVER-DOWN.md) for roster↔PIN, PIN↔cooldown, auth success |
| **Split pane shell** — header slot + body slot; chrome/content bounds | v1_auth keypad band = carousel card band; device-ui split layout | [`LAYOUT-SPLIT-PANE`](../layouts/LAYOUT-SPLIT-PANE.md) on all `SCR-*` |
| **PIN keypad geometry** (4 px pad/gap, 3×3 digits 1–9, lower slot height) | h03 island pad; v1_auth product shell | [`COMP-PIN-KEYPAD`](../components/COMP-PIN-KEYPAD.md) |
| **Persistent transition root** — list/detail/record hosts stay allocated; animate wrappers | h19 `s_list_clip`, `s_detail_wrap`, `s_record_reveal_host` | Stage manager owns one root per major flow; no per-tick clean |
| **Horizontal snap carousel** — scroll end sync, `scroll_lock` during programmatic snap, opacity peeks | p13 snap pages; v1_carousel + v1_connect roster | [`PANEL-BODY-ROSTER-CAROUSEL`](../panels/PANEL-BODY-ROSTER-CAROUSEL.md), [`COMP-USER-CARD`](../components/COMP-USER-CARD.md) |
| **Transport / timer Level 0 refresh** | h18 `ui_refresh`; v1 `v1_carousel_refresh_transport` | Diff view-models; no `lv_obj_clean` on progress ticks |
| **Connecting / offline chrome as property updates** | v1_connect dot row + offline ribbon visibility | [`EXP-LOGIN`](../../experiences/endpoint-login.md) connecting stage; poller vs stage separation |
| **Sketch capture FB** | h27 320×240×2; v1_record same dimensions | Declare budget in implementation package; tie to [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) |
| **Mood / status micro-motion** | p04 parallel `lv_anim` on existing objects | [`FOUNDATION-MOTION`](../foundations/FOUNDATION-MOTION.md) for non-stage feedback |
| **Locked-idle badge refresh** | h14 / h02 label updates without rebuild | [`SCR-EMPTY-HANGOUT`](../screens/SCR-EMPTY-HANGOUT.md) |

---

## Review queue

| ID / topic | One-line purpose | Priority | Status |
|---|---|---|---|
| **Measured** flash shortlist on device | Static assessments filed (see device-ui README); run appendix A probes on glass | High | ☐ static done |
| **Measured** `v1_carousel` inbox refresh | Level 1 diff vs clean rebuild — widget peak | High | ☐ |
| **v1_ui_common** | Ribbons, sleep, modals, shared paint helpers | High | ☐ |
| **h26** / **h27** / **h28** / **h13** / **p12** on device | Fill **Measured** in static `ASSESS-DEMO-*` | Medium | ☐ static filed |
| **h23** streaming PCM | Confirm idle-stop without 9.6 MiB cap | Medium | ☐ static filed — policy only |

All other island **h** / **p** / **x** ids have **SCAN** rows in this revision; promote to **DEEP** + `ASSESS-DEMO-*.md` when a pattern ships in v2 firmware.

---

## Quick reference — framebuffer sizes

| Artifact | Dimensions | Bytes (RGB565) | Notes |
|---|---|---|---|
| h19 record stub | 240×240 panel (no FB) | 0 | Visual placeholder only |
| h27 sketch | 320×240 | 153 600 | Full screen image widget |
| h26 shared draw | 320×240 | 153 600 | Persistent collaborative FB |
| h28 / p12 / h13 | 320×240 | 153 600 | CPU paint → `lv_image` |
| v1_record | 320×240 | 153 600 (`RECORD_FB_BYTES`) | Allocated SPIRAM preferring |
| p13 snap/card canvas | page-specific (e.g. 132×100) | ≪ full screen | Lab only |

---

## Changelog

| Rev | Date | Notes |
|---|---|---|
| 0.1 | 2026-10-11 | Initial ledger from prioritized demos + x02 shell modules |
| 0.2 | 2026-10-11 | Full **h** / **p** / **x** SCAN pass; summary verdict counts; heap risks; [`ASSESS-DEMO-h19`](ASSESS-DEMO-h19.md) |
