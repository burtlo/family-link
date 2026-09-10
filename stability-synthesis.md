# Stability Synthesis — family-link ESP32 + Web Twin

**Compiled:** 2026-09-10  
**Reviewer role:** Multi-pass stability analysis (analysis only — no firmware changes)  
**Sources:** [`first-interview.md`](first-interview.md), [`interviews.md`](interviews.md), agent transcripts (12 JSONL files), current tree at review time.

---

## 1. Executive summary

Across **15 distinct device-failure incidents** (INT-001–INT-015) mined from agent transcripts, failures cluster into five recurring classes: **LVGL memory lifecycle**, **touch/z-order competition**, **async state-machine races**, **full-screen `paint_*` rebuilds**, and **compile/flash success mistaken for device correctness**. One incident (**INT-014**, PIN stuck on `checking...`) remains **unresolved** after two fix attempts in the same session that also landed ~2100 lines of carousel changes in `x02_product_shell.c`.

The project’s dominant stability risk is not a single bug but a **development pattern**: large monolithic edits to a flag-heavy state machine, rapid flash cycles without serial-log verification, and web/firmware parity maintained by parallel copy-paste rather than shared contracts. The first PIN fix correctly identified a connectivity-probe vs login race but **did not fully close the async login lifecycle** (entry guard at `login_task_fn` still drops work without repaint), and **no runtime evidence** was captured before the user paused work.

**Highest-leverage improvements (prioritized):**

1. **Mandatory serial-log verification** before closing any device-touching task (PIN, carousel, connectivity).
2. **Ban success-path gating on volatile UI state** (`s_st == ST_PIN` and similar) after async handoff; use operation tokens / generation counters instead.
3. **Incremental LVGL updates** for carousel/transport; reserve full `paint_*` for screen transitions only.
4. **LVGL memory budget gate** for new screens/widgets (p13 pattern: lazy load, heap log at boot).
5. **Web/firmware parity checklist** with shared timing constants and behavior specs in `docs/BOX-UI.md`.

---

## 2. Known vs unknown matrix

| ID | Symptom | Status | Confirmed root / fix | Unverified / conflicting |
|----|---------|--------|----------------------|---------------------------|
| INT-001 | WiFi failed on boot | **Partial** | Placeholder `secrets.h` + 5 GHz vs 2.4 GHz | User creds not confirmed applied |
| INT-002 | Broken play icon; no swipe | **Fixed** | Rotated-rect icon; peek z-order; swipe handler | User confirmed touch only (line 22) |
| INT-003 | Multi-tap play after nav | **Fixed (agent)** | Chirp/codec contention; non-clickable disk | No user confirmation in transcript |
| INT-004 | Stale roster; PIN “locked” feel | **Implemented** | Async login + connection confidence spec | **Regressed** → INT-014 |
| INT-005 | Connecting dots misaligned / crawl | **Fixed** | Graphical dots; async probe; separate anim timer | User confirmed “great” (line 109) |
| INT-006 | p13 white screen on boot | **Fixed** | 64 KB LVGL heap exhausted by 28 screens at startup | Residual white flash → INT-007 |
| INT-007 | p13 white flash ~2 s | **Fixed** | Delete-before-load order; nav debounce | Agent-only confirmation |
| INT-008 | p13 triangles freeze | **Fixed** | 24 canvases → 4 PSRAM bitmap rows | Agent-only confirmation |
| INT-009 | p13 scroll tearing | **Partial** | SPI bandwidth; opt page with baked cards | Original page may still tear |
| INT-010 | p13 fonts all same size | **Fixed** | `sdkconfig` font enables vs defaults drift | — |
| INT-011 | Carousel touch/clamp/center | **Partial** | Snap math, vignettes, p13 port iterations | No explicit user “fixed” |
| INT-012 | Post-playback jump to first | **Fixed (agent)** | `request_transport_refresh` vs full repaint | User unconfirmed |
| INT-013 | Snap too fast / no in-betweens | **Partial** | 380–720 ms distance-scaled snap | User unconfirmed; INT-014 followed |
| INT-014 | PIN stuck `checking...` | **UNRESOLVED** | Race theory; partial guards applied | HTTP hang, task crash, L2249 drop, logs missing |
| INT-015 | x02 build failure | **Fixed** | Forward decl / dead code cleanup | Blocked device test only |

### KNOWN (high confidence)

- **INT-006/007/008:** LVGL 64 KB heap and eager widget creation cause white screen, flash, and freeze in p13 (`600bf418…jsonl:36,54,80`; `p13_ui_showcase.c` lazy-load fix).
- **INT-002/003/011:** Touch failures from overlay children, wrong event types, and audio-resource contention (`b6d4b1e5…:1`; `f03d3447…:1`; `03dbb406…:37`).
- **INT-005:** Blocking HTTP on `ui_task` caused connecting-dot animation stall/burst (`cc52795e…:75–102`).
- **INT-004→014:** Connection-confidence work (async PIN, 2.5 s timeout) predates carousel session; INT-014 is a **regression in the same flow** after massive x02 edits (`902a373a…:1`; `03dbb406…:115,129`).
- **INT-012:** Full `paint_carousel()` after playback reset scroll to 0 (`03dbb406…:88`; `x02_product_shell.c:1732`, `3044–3148`).
- **Agent pattern:** `make x02` + flash COM4 reported as success without serial capture (`first-interview.md` §5; `interviews.md` Notes §1).

### UNKNOWN (needs evidence)

- **INT-014 root cause after first fix:** Serial logs never captured; user still stuck at 3:38 PM (`03dbb406…:129`).
- Whether INT-012/013 fixes behave correctly on device (no user confirmation before next failure).
- INT-001: whether user updated `secrets.h` and reflashed.
- INT-009: whether opt page eliminated tearing on hardware.
- Server reachability / TLS / `who.c` host at flash time during INT-014 (`first-interview.md` open hypotheses 1, 7).
- Whether `JSON_CAP` 4096 truncates login inbox JSON on device (`x02_product_shell.c:72`).

### CONFLICTING / WEAK ACCOUNTS

- **INT-014 race fix “should work”** (agent line 127) vs **user “still not fixed”** (line 129) — theory not validated.
- **“Successful login always goes to carousel”** (agent) vs code still having **`if (s_st != ST_PIN \|\| s_elen != 4) continue`** at task entry (`x02_product_shell.c:2249–2251`) which abandons login without `request_repaint()` — potential **residual bug** not discussed in first interview.
- INT-006 marked fixed while INT-007 reported 4 minutes later — suggests first fix incomplete until load-order change.

---

## 3. Correlation map

### 3.1 Causal chains (prose)

**Chain A — Connection confidence → PIN async → carousel regression**

INT-004 moved PIN verify off the UI thread and added `checking...` UX per `docs/BOX-UI.md:307–313`. INT-005 fixed connecting-screen blocking in the same state machine. INT-011 then rewrote carousel (~+2100 lines) touching the same file and flags. INT-014 appeared **in the same transcript** immediately after INT-013 animation tuning — strongly suggesting **interaction between fresh carousel/state edits and the PIN login worker**, not an isolated PIN bug.

**Chain B — Full repaint destroys ephemeral UI state**

INT-012 (playback → jump to first) and INT-011 (touch/center) both involve `paint_carousel()` destroying the scroller and rebuilding widgets. INT-013 adjusted snap timing on top of that stack. Pattern: **`request_repaint()` → `lv_obj_clean(scr)` → scroll/focus/widget handles reset** unless explicitly preserved (`x02_product_shell.c:3048–3143`). INT-014 may share this class if login success calls `request_repaint()` but `ui_task` is blocked behind toast delay or paint fails on orphaned PIN widgets.

**Chain C — LVGL heap / lifecycle (p13 → x02 lessons)**

INT-006 white screen → INT-007 delete order → INT-008 canvas explosion. INT-009 tearing documents SPI limits for scroll animation — **same snap pattern ported to x02** in INT-011/013. p13 mitigations (lazy load, PSRAM bitmaps, load-then-delete) are **not systematically applied** to x02 carousel, which rebuilds many cards per paint.

**Chain D — Flash without verify**

INT-001 (WiFi creds), INT-003 (play fix unconfirmed), INT-011–014 (rapid COM4 flashes): agent declares success at compile/flash boundary. User discovers failure on device. Next agent patches symptom. **No closed-loop verification.**

### 3.2 Cluster diagram (reference)

See `interviews.md` mermaid (INT-004 → INT-014, INT-006 → INT-007/008, INT-011 → INT-012/014).

### 3.3 Cross-event flag / API surface

| Mechanism | Incidents | Files |
|-----------|-----------|-------|
| `s_pin_login_busy` | INT-004, INT-014 | `x02_product_shell.c:252,577–578,2234,2249,2266` |
| `enter_connecting_from_signin()` | INT-004, INT-005, INT-014 | `x02_product_shell.c:575–586,3598–3606` |
| `request_repaint()` vs `request_transport_refresh()` | INT-012, INT-013, INT-014 | `x02_product_shell.c:259–260,1732,3634–3642` |
| `signed_out_hangout_probe()` every 5 s | INT-004, INT-005, INT-014 | `ui_task` ~3604–3606; `BOX-UI.md:269,302` |
| LVGL 64 KB / lazy pages | INT-006–008, INT-010 | `p13_ui_showcase.c`; `600bf418…:36` |
| Touch overlay / `CLICKABLE` flags | INT-002, INT-003, INT-011 | `b6d4b1e5…:1`; `03dbb406…:37` |

---

## 4. Recurring anti-patterns

### 4.1 Device failure classes

| Class | Examples | Signature |
|-------|----------|-----------|
| **Memory budget** | INT-006, INT-008 | White screen, freeze, heap log missing |
| **Widget lifecycle order** | INT-007 | Periodic blank frame on navigation |
| **Touch z-order / event type** | INT-002, INT-011 | “Doesn’t respond” / needs multiple taps |
| **Audio resource contention** | INT-003 | Play fails until retry; silent codec open failure |
| **UI thread blocking** | INT-004, INT-005 (pre-fix) | Frozen animation, “locked up” feel |
| **Async state race** | INT-004, INT-014 | Stale label (`checking...`), skipped transition |
| **Full paint rebuild** | INT-012, INT-011 | Scroll/focus jump, touch target reset |
| **SPI / animation bandwidth** | INT-009, INT-013 | Tearing, “teleport” snap |
| **Config / secrets drift** | INT-001, INT-010 | WiFi fail, wrong fonts |
| **Build breaks device test** | INT-015 | `-Werror` implicit decl blocks flash |

### 4.2 Agent workaround patterns (observed)

1. **Compile succeeded → user reports broken → agent patches local symptom** — repeated INT-011 through INT-014 in one afternoon (`03dbb406…`).
2. **Monolithic rewrite instead of minimal fix** — user asked to “start over with p13 carousel” (line 58) after incremental patches failed.
3. **Flash as proof** — “Firmware is flashed to COM4” (lines 113, 127) without monitor output attached.
4. **Theory stated confidently without logs** — INT-014 race narrative (line 125–127) contradicted by user 27 minutes later (line 129).
5. **Parallel web + firmware edits without parity test** — `box.js` / `x02_product_shell.c` co-evolve; no automated diff of behavior constants.
6. **Flag accumulation** — `s_st`, `s_pin_login_busy`, `s_scroll_lock`, `s_carousel_locked`, `s_repaint`, `s_transport_dirty`, `s_server_online` interact opaquely (`first-interview.md` §4).

### 4.3 “Fix introduced regression” pattern

| Fix for | Later regression |
|---------|------------------|
| INT-004 async PIN | INT-014 checking hang |
| INT-006 lazy load | INT-007 white flash (delete order) |
| INT-011 carousel rewrite | INT-012, INT-013, INT-014 same session |
| INT-014 race guard on `enter_connecting` | Entry guard at `login_task_fn:2249` still drops work silently |

---

## 5. Recommended behaviors & design choices (prioritized)

### P0 — Process (immediate, no architecture change)

1. **Serial-log gate before task close**  
   For any PIN, connectivity, or carousel change: capture 30 s of monitor during repro; grep for `login`, HTTP status, `s_st` transitions, heap lines. **Do not claim fixed without log excerpt or explicit user confirmation.**

2. **Explicit user confirmation protocol**  
   After flash, ask user to perform one scripted action (e.g. “enter PIN 1234, wait 5 s, report screen”). Record outcome in commit/session notes.

3. **Freeze scope during auth bugs**  
   When debugging INT-014-class issues, **no carousel/feature edits** in the same branch until PIN login verified.

### P1 — State machine / async design

4. **Operation tokens for async login**  
   Replace `s_pin_login_busy` + `s_st` checks with monotonic `login_generation`: PIN submit bumps generation; worker captures `(user_id, pin, gen)`; UI applies result only if `gen` still current; **always** schedule repaint on worker exit (success, failure, or cancel).

5. **Never `continue` async worker without UI reconciliation**  
   Current `login_task_fn:2249–2251` clears busy and continues with no `request_repaint()` — violates `BOX-UI.md` “never stuck or silently broken” (`docs/BOX-UI.md:286`).

6. **Connectivity probe defers to in-flight login**  
   Extend INT-014 guard: while login active, **freeze** `s_st` transitions and probe side effects (already partial at `575–578`; audit all `s_st =` writers in `signed_out_pre_auth` paths).

7. **Repaint before blocking delays**  
   Keep toast/`vTaskDelay(2500)` after paint (applied at `3634–3652`) for all success paths.

### P2 — UI architecture

8. **Incremental carousel updates**  
   - Screen transition: full `paint_carousel` once.  
   - Playback end / transport: `ui_refresh_transport()` only (INT-012 pattern at `1732`).  
   - Inbox delta: swap card data, don’t `lv_obj_clean(scr)`.

9. **Preserve scroll/focus contract**  
   Document and test: `keep_scroll` / `keep_focus` in `paint_carousel` (`3048–3143`) must run for **every** rebuild path; add assert log when `restore_scroll` false after session login.

10. **LVGL memory budget checklist for new UI**  
    Before merging demo pages: log `esp_get_free_heap_size()` after build; cap widgets per screen; prefer PSRAM bitmaps over live canvases (INT-008 lesson).

### P3 — Web/firmware parity

11. **Single source for timing constants**  
    Snap durations, probe timeouts (2.5 s), retry intervals (5 s) should match between `box.js`, `x02_product_shell.c`, and `docs/BOX-UI.md`.

12. **Twin-first repro**  
    Reproduce UI/state bugs in web twin when possible before flashing; web lacks FreeRTOS races but validates snap math and UX sequence.

### P4 — Tooling

13. **Flash script post-verify hook**  
    Optional `scripts/flash.py --monitor-seconds N` to capture boot log after flash.

14. **Structured ESP_LOGI for auth FSM**  
    Log `{event, s_st, busy, http_status, gen}` on every PIN phase — enables transcript-free postmortems.

---

## 6. Candidate rules/skills (draft text)

### 6.1 Cursor rule: `device-verify-before-done.mdc`

```markdown
---
description: Require device verification before closing firmware/UI tasks
globs:
  - firmware/**
  - demos/server/v1_product/web/**
---

## Device task completion gate

When a task changes firmware or web twin behavior that affects the physical BOX-3:

1. Build must succeed (`make x02` or relevant DEMO).
2. After flash, capture serial monitor output (≥20 lines) OR obtain explicit user confirmation of the scripted test.
3. Do NOT mark the task complete based on compile/flash alone.
4. If the user reports failure, capture their exact screen state and timestamp before attempting a second fix.
5. For PIN/connectivity changes: run the PIN entry script (4 digits → expect carousel or `wrong pin` or connecting within 3 s per BOX-UI.md).

Forbidden closure phrases without evidence: "should work", "flashed successfully", "try again".
```

### 6.2 Cursor rule: `lvgl-incremental-ui.mdc`

```markdown
---
description: Prefer incremental LVGL updates over full screen rebuilds
globs:
  - firmware/demos/x02_product_shell.c
  - firmware/demos/p13_ui_showcase.c
---

## LVGL update policy

- Use `request_repaint()` / full `paint_*` only for **screen state changes** (ST_PIN → ST_CAROUSEL, etc.).
- For carousel playback, transport, and focus visuals: use `request_transport_refresh()` or in-place style updates.
- Never call `lv_obj_clean(scr)` on the active carousel unless scroll position and focus are saved and restored (see `paint_carousel` keep_scroll pattern).
- New screens must log free heap after build; if adding >10 widgets, justify or split lazy-load like p13.
- CONFIG_LV_MEM_SIZE is 64 KB — treat as hard budget unless sdkconfig changed with measurement.
```

### 6.3 Cursor rule: `async-state-no-stale-gates.mdc`

```markdown
---
description: Async workers must not gate success on stale UI state flags
globs:
  - firmware/demos/x02_product_shell.c
---

## Async state machine rules

- Worker tasks must capture inputs at enqueue time (user_id, pin, operation_id).
- Success handlers must NOT require `s_st == ST_*` unless transitioning from a confirmed sub-state.
- Every worker exit path must call `request_repaint()` or apply a direct LVGL update — never silent `continue`.
- While `login_operation_active`, block `enter_connecting_from_signin` and any `s_st` change away from PIN except explicit cancel/timeout UI.
- Align with docs/BOX-UI.md Connection confidence and PIN verify sections.
```

### 6.4 SKILL.md draft: `device-test-after-flash`

```markdown
# Device test after flash

## When to use
After `make flash DEMO=x02` or any firmware change affecting PIN, carousel, WiFi, or connectivity.

## Steps
1. Run flash with monitor: `python scripts/flash.py --demo x02 --port COM4` (adjust port).
2. Boot checklist:
   - [ ] WiFi join log or ST_WIFI_ERR screen
   - [ ] Connecting dots animate smoothly (not frozen 15 s)
   - [ ] Roster appears after server probe
3. PIN checklist:
   - [ ] 4 digits → `checking...` within 1 tap
   - [ ] Within 3 s: carousel OR `wrong pin` OR connecting (never infinite checking)
4. Carousel checklist:
   - [ ] Tap non-center card → animates to center
   - [ ] Play centered message → after end, same card still centered
5. Paste serial excerpt showing login HTTP status or state transitions into session notes.

## Failure handling
If any step fails, do not apply unrelated fixes in the same commit. Capture logs first.
```

### 6.5 SKILL.md draft: `web-firmware-parity-check`

```markdown
# Web/firmware parity check

## When to use
After editing both `demos/server/v1_product/web/box.js` and `firmware/demos/x02_product_shell.c`.

## Compare
| Constant | Web | Firmware | BOX-UI.md |
|----------|-----|----------|-----------|
| Login/probe timeout | | CONNECT_PROBE_MS | 2.5 s |
| Hangout retry | | CONNECT_RETRY_MS | 5 s |
| Snap duration range | SNAP_SCROLL_MS_* | SNAP_SCROLL_MS* | — |

## Manual twin test
1. Open web twin `/box/`
2. Run same PIN + carousel script as device checklist
3. Note any behavioral divergence for spec update before flash
```

---

## 7. Evidence appendix

### 7.1 Primary documents

| Document | Role |
|----------|------|
| `first-interview.md` | Deep dive INT-014, first fix theory, open hypotheses |
| `interviews.md` | INT-001–015 catalog, cluster table, transcript index |

### 7.2 Code references (current tree)

| Topic | File | Lines |
|-------|------|-------|
| JSON buffer cap | `firmware/demos/x02_product_shell.c` | 72, 254 |
| HTTP read timeout | `firmware/demos/x02_product_shell.c` | 902–958 |
| `enter_connecting` + busy guard | `firmware/demos/x02_product_shell.c` | 575–586 |
| PIN key → checking | `firmware/demos/x02_product_shell.c` | 2202–2239 |
| Login worker entry gate | `firmware/demos/x02_product_shell.c` | 2242–2314 |
| Login success → carousel (no ST_PIN gate) | `firmware/demos/x02_product_shell.c` | 2267–2290 |
| Playback → transport refresh only | `firmware/demos/x02_product_shell.c` | 1732 |
| `paint_carousel` scroll preserve | `firmware/demos/x02_product_shell.c` | 3044–3148 |
| `ui_task` probe loop + repaint order | `firmware/demos/x02_product_shell.c` | 3598–3653 |
| Login task start before ui_task | `firmware/demos/x02_product_shell.c` | 3755–3758 |
| Connection confidence spec | `docs/BOX-UI.md` | 284–313 |
| PIN verify async spec | `docs/BOX-UI.md` | 307–313 |
| Probe timing spec | `docs/BOX-UI.md` | 269, 302 |

### 7.3 Transcript JSONL line index (user failure reports)

| Ref | Transcript UUID | Line | User symptom (abbrev) |
|-----|-----------------|------|------------------------|
| INT-001 | `52e3594e-e223-4f12-9b44-1970bc23d405` | 54 | wifi failed |
| INT-002 | `b6d4b1e5-ad53-487a-9896-aae269083d9c` | 1 | broken play; no swipe |
| INT-002 ✓ | same | 22 | touch events restored |
| INT-003 | `f03d3447-9f5a-403a-b12b-e75795cbdb37` | 1 | multi-tap play |
| INT-004 | `902a373a-f055-4282-ae10-5799d2496292` | 1 | stale roster; PIN locked feel |
| INT-005 | `cc52795e-4e8c-433e-8907-285bfca99888` | 75,87,94,102 | connecting dots issues |
| INT-005 ✓ | same | 109 | “This is great.” |
| INT-006 | `600bf418-3011-4f93-bc32-861037455069` | 36 | white screen |
| INT-007 | same | 54 | white refresh ~2 s |
| INT-008 | same | 80 | triangles freeze |
| INT-010 | same | 94 | fonts wrong size |
| INT-009 | `3fa0fee1-7b62-4adb-bbec-1d7a9817c10b` | 1 | scroll tearing |
| INT-011 | `03dbb406-b06f-435e-a502-85ba0fa3bece` | 1,37,45,58 | carousel touch/center |
| INT-012 | same | 88 | jump to first after play |
| INT-013 | same | 105 | snap too fast |
| INT-014 | same | 115 | checking… no transition |
| INT-014 | same | 129 | still not fixed; pause |
| INT-015 | `266ef308-3e88-48ca-aa21-8a152bff14c3` | 50 | build errors (terminal paste) |

### 7.4 Agent closure without user verify (examples)

| Transcript | Line | Agent claim |
|------------|------|-------------|
| `03dbb406…` | 113 | “Firmware is flashed to COM4” (INT-013) |
| `03dbb406…` | 127 | “Try signing in again; … within a few seconds” (INT-014 fix) |
| `600bf418…` | 54 | White screen fix narrative (INT-006) |

---

## 8. After re-evaluation

Second pass over `first-interview.md` + `interviews.md` + current `x02_product_shell.c` surfaced connections **not fully explicit** in the first interview:

### 8.1 New theory: login worker entry drop → eternal `checking...`

The first fix removed `s_st == ST_PIN` gating on **success** (`first-interview.md` §2, `x02_product_shell.c:2267`) but **left** this at worker entry:

```c
if (s_st != ST_PIN || s_elen != 4) {
    s_pin_login_busy = false;
    continue;  // no request_repaint()
}
```

(`x02_product_shell.c:2249–2251`)

If the worker wakes after any `s_st` or `s_elen` mutation (probe edge, race, or memory corruption), the UI can remain on PIN widgets with `checking...` text while `s_pin_login_busy` is false — **matching INT-014 symptom without requiring HTTP failure**. This is consistent with user report after race fix (transcript `03dbb406…:129`).

**Evidence:** `first-interview.md:72–81` lists orphan-widget hypothesis (#5) but not this specific code path; code inspection confirms path exists post-fix.

### 8.2 New theory: toast + repaint ordering still delays first carousel paint

Repaint-before-toast was moved (`x02_product_shell.c:3634–3652`), but successful login sets `s_toast_pending` twice (PIN reset hint + send hint) before `request_repaint()` at `2288`. Toast still blocks `ui_task` for 2.5 s **after** first paint — unlikely to cause eternal checking, but can confuse user timing (“within a few seconds” in transcript line 127).

### 8.3 INT-011 session size correlates with INT-014

`first-interview.md:121` and `interviews.md:INT-011` — ~2100 line delta in one file in the same session as INT-014. **Correlation:** higher regression probability when auth and carousel share `ui_task`, `login_task_fn`, and global flags. Recommendation: **separate modules or critical-section docs** before further carousel work.

### 8.4 p13 → x02 port without memory/animation audit

INT-009 documented SPI tearing for snap-grad (`3fa0fee1…:1`); INT-011 ported pattern to x02. No evidence of bake-in-PSRAM or scroll-lock audit on x02 at port time. **Risk:** performance/touch issues masquerading as logic bugs.

### 8.5 INT-004 spec vs INT-014 behavior gap

`docs/BOX-UI.md:313` requires network failure → **connecting**, not frozen pad. Eternal `checking...` is a **spec violation** whether caused by race, HTTP hang, or worker drop — useful as acceptance criterion for any future fix.

### 8.6 Re-evaluation references

| Connection | first-interview | interviews.md | Code / transcript |
|------------|-----------------|---------------|-------------------|
| Entry gate residual | §2 open hypotheses (partial) | INT-014 theory | `x02_product_shell.c:2249–2251` |
| Spec violation | §1 expected behavior | INT-004, INT-014 | `BOX-UI.md:313` |
| Session scope risk | §4 risk pattern | INT-011 → INT-014 | `03dbb406…:58,115` |
| Worker drop + orphan UI | §2 hypothesis #5, #7 | INT-014 unresolved | `03dbb406…:115,129` |

---

*End of stability synthesis. No firmware changes were made during this review.*
