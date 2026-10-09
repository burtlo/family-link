# First Interview — PIN “checking…” hang and carousel session failures

**Status:** Work paused at user request (2026-09-10). Problem **not verified fixed** after two fix attempts.

**Primary transcript:** [carousel + PIN session](03dbb406-b06f-435e-a502-85ba0fa3bece)  
**Primary firmware file:** `firmware/demos/x02_product_shell.c` (~+2100 lines vs committed baseline)  
**Primary web twin:** `demos/server/v1_product/web/box.js`, `box.css`, `index.html`

---

## 1. Event under investigation (most recent, unresolved)

### User report
- **When:** 2026-09-10 ~3:11 PM local  
- **Symptom:** After entering 4-digit PIN, screen shows **`checking...`** and **does not transition** to inbox/carousel.  
- **Transcript ref:** `agent-transcripts/03dbb406-b06f-435e-a502-85ba0fa3bece/03dbb406-b06f-435e-a502-85ba0fa3bece.jsonl` **line 115** (user message).

### Follow-up
- **When:** 2026-09-10 ~3:38 PM local  
- **User statement:** “The problem is still not fixed.” Work explicitly paused; no further debugging requested.  
- **Transcript ref:** same file **line 129**.

### Expected behavior (spec)
From `docs/BOX-UI.md` and product spec:
- 4th digit → four dots + status **`checking...`**
- `POST /v1/session/login` on **worker task**, **2.5 s** probe timeout
- Ignore keypad while checking
- On success → carousel with inbox

**Code refs:**
- `docs/BOX-UI.md` — PIN verify / connection confidence sections (search: `checking...`, `2.5 s`)
- `firmware/demos/x02_product_shell.c` — `on_pin_key` ~L2194, `login_task_fn` ~L2234, `set_status("checking...", ...)` ~L2228

---

## 2. First fix attempt — theory and changes

### Theory (first attempt)
**Race between async PIN login and background connectivity/state machine.**

While `login_user()` runs HTTP POST (~2.5 s) on `login_task`:
1. UI shows “checking…” on PIN screen (`on_pin_key` sets `s_pin_login_busy = true`).
2. Parallel `ui_task` loop may call `signed_out_hangout_probe()` / `enter_connecting_from_signin()` when server appears offline.
3. `enter_connecting_from_signin()` previously:
   - Cleared PIN entry (`s_elen`, `s_entry`)
   - Set `s_pin_login_busy = false`
   - Changed `s_st` from `ST_PIN` → `ST_CONNECTING`
4. When HTTP login **succeeded**, `login_task_fn` required `s_st == ST_PIN` before transitioning to carousel. If state had changed, success handling was **skipped silently** — UI left on stale PIN widgets still showing “checking…”.

Secondary contributors hypothesized:
- **Full `request_repaint()` after playback** and other events causing scroll/focus resets (addressed in same session, separate bug).
- **HTTP read loop** without elapsed timeout — possible indefinite hang (added read timeout).
- **Login task created late** in `app_main` — possible lost semaphore / ordering issue (moved earlier).
- **Toast handler** in `ui_task` blocking 2.5 s **before** repaint — delayed screen updates after login (reordered to repaint first).

### Code changes (first fix, 2026-09-10)
File: `firmware/demos/x02_product_shell.c`

| Change | Approx. location | Intent |
|--------|------------------|--------|
| `enter_connecting_from_signin()` returns early if `s_pin_login_busy` | ~L575 | Prevent connectivity probe from clobbering in-flight login |
| Success path no longer gated on `s_st == ST_PIN` | `login_task_fn` ~L2257 | Transition to carousel even if state changed during HTTP |
| `request_transport_refresh()` instead of `request_repaint()` after playback | `playback_task` ~L1654 | Avoid full carousel rebuild resetting scroll |
| Preserve scroll/focus in `paint_carousel()` | ~L2944 | Reduce jump-to-first-message on incidental repaints |
| HTTP read timeout in `http_json_timeout()` | ~L944 | Bound stall on partial response |
| Login task started before `ui_task` | `app_main` ~L3745 | Ensure worker exists before PIN entry |
| Repaint block moved before toast delay in `ui_task` | ~L3623 | Faster visual transition after login |

**Build/flash:** `make x02` succeeded; flashed COM4 (multiple times in session).  
**User outcome:** Still stuck on “checking…” — **fix not confirmed**.

### Open hypotheses (after failed fix)
1. **HTTP never completes** — server unreachable, TLS, wrong host/IP in flashed firmware (`firmware/common/who.c`, sdkconfig, Makefile).
2. **Login response truncated** — `JSON_CAP` 4096; large inbox JSON → parse failure → treated as failed login (would expect “wrong pin”, not eternal checking, unless UI not updated).
3. **`login_task` not scheduled / crashed** — stack overflow (8192), watchdog; `s_pin_login_busy` stays true forever.
4. **`request_repaint()` not processed** — `ui_task` blocked on `play_chirp`, sleep overlay, or LVGL lock deadlock.
5. **LVGL `set_status` on destroyed widgets** — PIN screen not repainted; label orphaned after partial state change.
6. **Fix incomplete** — race still exists via another path that clears state without respecting `s_pin_login_busy`.
7. **Server-side** — login returns non-200 slowly; failure path also fails to repaint (needs verification).

**Not done before pause:** Serial monitor capture during PIN entry, HTTP status logging in `login_task_fn`, debugger/watch on `s_st`, `s_pin_login_busy`, `s_repaint`.

---

## 3. Related failures in same agent session (same transcript)

These were reported, fixed in-session, and may share root patterns (full repaint, state races, touch competition):

### 3.1 Carousel tap-to-center / animation
- **User report (line 37):** Cards don’t center except first; play vs card touch competition.  
- **Iterations:** 176×168 cards + head-only tap → vignettes → full p13 rewrite (132×100, transport below track).  
- **Theory:** Wrong scroll math (`offsetLeft` vs padding), vignettes blocking touches, `SHORT_CLICKED` vs `CLICKED`, per-card play intercepting taps.

### 3.2 Post-playback snap to first message
- **User report (line 88):** After playback, carousel jumps to first message.  
- **Theory:** `request_repaint()` → `paint_carousel()` destroys scroller (scroll=0) → `carousel_snap_to_focus()` from zero; `parse_inbox` resetting `s_focus` from stale `last_viewed_seq`.  
- **Fix applied:** Transport-only refresh after playback; preserve scroll on rebuild. **User confirmation unknown.**

### 3.3 Animation too fast / no in-between motion
- **User report (~line 108):** Cards don’t look like they move through queue.  
- **Fix applied:** 380–720 ms distance-scaled snap; scroll-based visual focus during animation; scale/opacity on cards (web + firmware). **User confirmation unknown.**

---

## 4. Code surface area touched (session aggregate)

```
 Makefile                               |    3 +
 demos/server/v1_product/web/box.css    |  346 +++--
 demos/server/v1_product/web/box.js     |  542 ++++++--
 demos/server/v1_product/web/index.html |   40 +-
 docs/BOX-UI.md                         |  116 +-
 docs/plans/v1-product-spec.md          |   38 +-
 firmware/common/who.c                  |   17 +
 firmware/demos/x02_product_shell.c     | 2112 +++++++++++++++++++---
 firmware/main/CMakeLists.txt           |    8 +
 firmware/sdkconfig.defaults            |   20 +
 scripts/flash.py                       |   44 +-
```

**Risk pattern:** Large, iterative edits to monolithic `x02_product_shell.c` with multiple concurrent state flags (`s_st`, `s_pin_login_busy`, `s_scroll_lock`, `s_carousel_locked`, `s_repaint`, `s_transport_dirty`, `s_server_online`) and **full-screen `paint_*` rebuilds** instead of incremental UI updates.

---

## 5. Agent “workaround” pattern observed

Repeated cycle in this and prior sessions:
1. User reports device misbehavior after flash.
2. Agent identifies local cause (touch targets, scroll math, race, memory, etc.).
3. Agent patches firmware/web, `make x02` succeeds, flash to COM4.
4. New or residual failure reported; agent patches again.

**Compile success ≠ device correctness.** No automated on-device test; no serial-log verification loop documented in session.

---

## 6. Reference index for future extraction

| Kind | Location | Notes |
|------|----------|-------|
| User PIN hang report | `03dbb406...jsonl:115` | char offset N/A (JSONL line) |
| User “still not fixed” | `03dbb406...jsonl:129` | pause request |
| PIN checking UI string | `x02_product_shell.c` ~L2228 | `set_status("checking...", UI_TEXT_MUT)` |
| Login worker | `x02_product_shell.c` ~L2234–2310 | `login_task_fn` |
| State race guard | `x02_product_shell.c` ~L575–584 | `enter_connecting_from_signin` |
| UI loop repaint | `x02_product_shell.c` ~L3548–3704 | `ui_task` |
| p13 reference carousel | `firmware/demos/p13_ui_showcase.c` ~L1065–1430 | snap grad pattern |
| BOX-UI PIN spec | `docs/BOX-UI.md` | search `checking...` |
| Prior white-screen (p13) | `600bf418...jsonl:36–53` | LVGL heap exhaustion |
| Prior connectivity UX | `902a373a...jsonl:1` | roster/PIN when server offline |

---

## 7. Interview meta

- **Interviewer role:** Agent (carousel/PIN session)  
- **Interviewee:** User (device operator)  
- **Confidence:** Medium on race theory; **low** on root cause of persistent “checking…” after fix — **not validated with logs**  
- **Recommended next evidence:** Serial log lines containing `login`, HTTP status, `s_st` transitions; timestamp from PIN entry to hang duration (>2.5s suggests hang vs failed UI update)
