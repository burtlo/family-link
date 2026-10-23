# Demo explorer index — legacy v1 POC island catalog

Status: working navigation artifact
Revision: **0.3**
Owner: Product / firmware agents
Evidence: static scan of **51** island demo sources in the legacy v1 POC firmware archive
Verdicts and LVGL depth: [`DEMO-PATTERN-LEDGER`](DEMO-PATTERN-LEDGER.md) (rev 0.2)
Measurement template: [`TEMPLATE-LVGL-SCREEN-ASSESSMENT`](TEMPLATE-LVGL-SCREEN-ASSESSMENT.md)

This index groups **island demos** (`h**`, `p**`, `x**`) by product theme so agents
and owners can pick flash targets for V2 UI work without opening the archive tree.
It does **not** replace experience specs or screen IDs (`SCR-*`).

**Flash:** use host demo targets (for example `make flash DEMO=h19`) per
[`config/host.defaults.ini`](../../../../config/host.defaults.ini). Shell demo
**x02** is a build-id stub; runtime UI modules are analyzed in the ledger as
`v1_connect`, `v1_auth`, `v1_carousel`, and `v1_record`.

## At a glance

| Prefix | Count | Role |
|---|---:|---|
| **h** | 34 | Hardware, network, inbox, voice, sketch, storage qualification |
| **p** | 13 | Persona, motion, notice chrome, LVGL lab (mostly offline) |
| **x** | 4 | Product-shaped shells and Opus build probes |
| **Total** | **51** | Matches ledger island catalog (rev 0.2) |

## Ledger cross-check (rev 0.2)

| Check | Result |
|---|---|
| Every island `.c` has a ledger row | **Yes** — 51/51 |
| DEEP-reviewed UI patterns | **12** island/shell rows (see ledger “deep-reviewed”) |
| Static `ASSESS-*` (flash + sketch/media tiers) | **15** — see README assessments table |
| Measured on device (filled table) | **0** — run lab probes per [`FOUNDATION-LVGL-BUDGET`](../foundations/FOUNDATION-LVGL-BUDGET.md) appendix |
| Explorer-only gaps | **p01** / **p05** lack block purpose comments (purpose below from code + ledger); **h32** / **h35** / **h37** / **h38** use serial/JSON fixture completion, not `-- PASS` UART; **x02** stub has no in-file PASS (shell journey) |

No missing demo IDs relative to the ledger. Promote any **ADOPT** / **ADAPT** row to
**DEEP** + `ASSESS-DEMO-<id>.md` before shipping a matching V2 screen.

---

## Product shell & integration

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **x01** | Monolithic BOX-3 shell wiring locked / PIN / inbox / record / hangout | Level 2 `lv_obj_clean` per app state; preview FB | `demo_pass("x01")` after shell journey | **AVOID** |
| **x02** | Stage router: connecting → roster → PIN → carousel → record → send | Level 2 dispatch; modules own paint | (shell; no island `demo_pass` in stub) | **ADAPT** |
| **x02-opus-dep** | x02 link with Opus dependency negative control | Delegates to x02 main | Build/size qualification | **SKIP** |
| **x02-opus-probe** | x02 Opus footprint probe | Delegates to x02 main | Build/size qualification | **SKIP** |

---

## Auth, PIN, and API smoke

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **h03** | GT911 PIN pad unlock without audio or message body | Persistent screen; keypad painted once | `demo_pass("h03")` on correct PIN | **ADAPT** |
| **h15** | PIN pad then latest inbox text; locked shows **N new** only | Level 2 clean on pad and on body reveal | `demo_pass("h15")` after unlock + fetch | **ADAPT** |
| **h06** | Wi-Fi STA join, 60 s associated with IP | No display | `demo_pass("h06")` after 60 s up | **SKIP** |
| **h07** | HTTP `GET /v1/me` good bearer then bad | `board_status` only | `200 + 401` then `demo_pass("h07")` | **SKIP** |
| **h16** | HTTPS `GET /v1/me` (skip-verify LAN) | `board_status` only | `200 + 401` then `demo_pass("h16")` | **SKIP** |

---

## Locked idle, inbox chrome, and navigation

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **h02** | Locked-idle **N new** screen and PWM dim | One-shot labels; no teardown | `demo_pass("h02")` after paint | **ADOPT** |
| **h14** | Heartbeat + WS inbox badge; no message body | Clean once; badge **Level 0** | `demo_pass("h14")` when hello + heartbeat OK | **ADAPT** |
| **h10** | Server playhead survives device reboot | `board_status` only | Second boot unread=1 → `demo_pass("h10")` | **SKIP** |
| **h19** | Scrollable inbox list + transition showcase + record stub | Persistent root; vertical scroll; transform stage anims | `-- PASS h19` after list→detail and Back | **ADOPT** |
| **h18** | Playback screen for inbound audio (metadata, bar, ROOMVOL) | Mixed clean on message change; **Level 0** transport refresh | JSON on screen (`demo_pass("h18")`); play is manual check | **ADOPT** |
| **h13** | Inbox image preview full-screen RGB565 | `lv_obj_clean` + `lv_image` | `demo_pass("h13")` after paint | **ADAPT** |
| **p08** | Fake inbox notice: chirp + badge 1 then 2 | Badge/caption **Level 0** | `-- PASS p08` after badge 2 | **ADOPT** |

---

## Presence, hangout, and live voice UI

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **h20** | Mute latch → open/away; peer on heartbeat | Clean once; two-card **Level 0** | `-- PASS h20` after 200 with peer object | **ADOPT** |
| **h21** | Duplex voice Mazi ↔ Arlo with friend card + ROOMVOL | Clean once; **Level 0** status | `-- PASS h21` ~10 frames each way | **ADOPT** |
| **h11** | Hangout PTT half-duplex via server | No LVGL | ~10 frames each way | **SKIP** |
| **h12** | h11 + LCD refresh decoupled from PCM | `board_status` on **500 ms** task | `-- PASS h12` if both directions OK | **ADAPT** |
| **h04** | Mute key hold-to-talk GPIO edges (not toggle) | Single label **Level 0** | `demo_pass("h04")` after press/release sequence | **ADAPT** |
| **h17** | Live button panel + mute GPIO + chirps | Persistent rows; **Level 0** values | `-- PASS h17` chirp + both mute states | **ADOPT** |

---

## Voice capture, upload, and codec (low UI)

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **h05** | Hold mute: mic loopback to speaker | `board_status` / minimal | `demo_pass("h05")` after ≥300 ms take | **SKIP** |
| **h08** | Hold red circle → WAV upload | `board_status` only | `-- PASS h08` on HTTP 200 | **SKIP** |
| **h09** | Download inbox audio and play | `board_status` only | `played PASS` + `demo_pass("h09")` | **SKIP** |
| **h22** | Diary: chunked WAV while unmuted | `board_status` only | `-- PASS h22` first HTTP 200 | **SKIP** |
| **h23** | Tap record/stop + idle-stop upload | `board_status` only | `-- PASS h23` on HTTP 200 | **SKIP** |
| **h24** | Heartbeat ships device event log ring | `board_status` only | `-- PASS h24` 200 acks log line | **SKIP** |
| **h25** | Chipmunk playback effect | `board_status` only | `demo_pass("h25")` after playback | **SKIP** |
| **h29** | Async voice ping-pong hold-to-send | Home clean once; status **Level 0** | `-- PASS h29` send or inbound play | **ADAPT** |
| **h30** | Local Opus encode/decode soak | `board_status` only | `SOAK PASS h30` / `demo_pass("h30")` | **SKIP** |
| **h31** | Opus chunk upload + range play; **only full Opus message E2E** — see [active proof doc](../../../demos/opus-chunk-end-to-end-h31.md) | `board_status` only | `SOAK PASS h31` / `demo_pass("h31")` | **SKIP** (streaming; owner-validated Oct 2026) |

---

## Sketch, draw, and full-frame media

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **h27** | Store-and-forward drawing note (timed record + replay) | Level 2 clean per stage; **320×240×2** FB | `-- PASS h27` send or inbound point | **ADAPT** |
| **h26** | Real-time shared drawing over WS | `lv_image` + FB; clean on clear | `-- PASS h26` first friend stroke | **ADAPT** |
| **h28** | Short CPU-painted clip on LCD (~5 s loop) | Timer blit to `lv_image` | `-- PASS h28` first complete playthrough | **ADAPT** |

---

## Persona, motion, and chrome lab

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **p01** | Static geometric mascot face | ~15 `lv_obj` shapes; one paint | `demo_pass("p01")` | **ADAPT** |
| **p02** | Blink + breathe idle; FPS log | **Level 0** anims | `-- PASS p02` after 10 s metrics | **ADOPT** |
| **p03** | Mood faces via hard cuts on mute cycle | Rebuild per mood | `-- PASS p03` all five states | **AVOID** |
| **p04** | Same moods with 200–400 ms tweens | Persistent face; `lv_anim` | `-- PASS p04` all five states | **ADOPT** |
| **p05** | Press / listen / poke face reactions | Property updates on persistent face | `demo_pass("p05")` | **ADOPT** |
| **p06** | ES8311 chirp suite | Minimal LVGL | `-- PASS p06` all six chirps | **SKIP** |
| **p07** | Pet loop blink + mute listen face | Timer **Level 0** | `-- PASS p07` mute cycle + 10 s idle | **ADOPT** |
| **p09** | Mouth flap during fixture tone | Widget mouth + audio | `demo_pass("p09")` if clip clean | **ADAPT** |
| **p10** | Packed parent portrait RGB565 | Static `lv_image` + caption | `demo_pass("p10")` | **ADOPT** |
| **p11** | Greeting WAV on mute tap | Portrait persists | `demo_pass("p11")` | **SKIP** |
| **p12** | Tamagotchi in CPU FB (tear-free motion) | Single **320×240×2** blit | `-- PASS p12` bounce + listen unmuted | **ADAPT** |
| **p13** | LVGL lab: scroll, snap, modals, canvas rows | Per-page screens; horizontal snap | `-- PASS p13` one next-page | **ADOPT** |

---

## Storage qualification (no product UI)

| ID | Purpose (one line) | LVGL pattern | PASS criterion | Ledger |
|---|---|---|---|---|
| **h32** | Deterministic on-chip FAT/WL qualification | No LVGL | Fixture serial completion | **SKIP** |
| **h35** | SDMMC discovery (Stage A) | No LVGL | Fixture serial completion | **SKIP** |
| **h37** | SDMMC read-only metadata transport | No LVGL | Fixture serial completion | **SKIP** |
| **h38** | Bounded FAT32 setup and FS I/O | No LVGL | Fixture serial completion | **SKIP** |

---

## Recommended flash set for V2 UI (7 demos)

Prioritize **ADOPT** ledger verdicts that map directly to [`device-ui/README.md`](../README.md)
screens, motion, and carousel work. Pair **h19** with a measured
[`ASSESS-DEMO-h19`](ASSESS-DEMO-h19.md) run when validating transitions on glass.

| Priority | ID | Why flash for V2 UI |
|---:|---|---|
| 1 | **h19** | [`ASSESS-DEMO-h19`](ASSESS-DEMO-h19.md) — cover-down list/detail |
| 2 | **x02** | [`ASSESS-SHELL-x02`](ASSESS-SHELL-x02.md) — stage router + modules |
| 3 | **h03** | [`ASSESS-DEMO-h03`](ASSESS-DEMO-h03.md) — PIN touch geometry |
| 4 | **p13** | [`ASSESS-DEMO-p13`](ASSESS-DEMO-p13.md) — snap / scroll lab |
| 5 | **p04** | [`ASSESS-DEMO-p04`](ASSESS-DEMO-p04.md) — mood tweens |
| 6 | **h18** | [`ASSESS-DEMO-h18`](ASSESS-DEMO-h18.md) — transport Level 0 |
| 7 | **p10** | [`ASSESS-DEMO-p10`](ASSESS-DEMO-p10.md) — portrait asset |
| 8 | **h14** | [`ASSESS-DEMO-h14`](ASSESS-DEMO-h14.md) — live badge **Level 0** |
| 9 | **h02** | [`ASSESS-DEMO-h02`](ASSESS-DEMO-h02.md) — idle typography + dim smoke |

Skip for pure UI iteration (still useful for firmware): **h06–h11**, **h22–h25**, **h30–h31**, storage **h32+**, and **p06** / **p11** per ledger **SKIP** list.

---

## Sketch / media tier (second pass)

Flash when implementing [`sketch-record-and-playback`](../../experiences/sketch-record-and-playback.md) or glass bandwidth limits.

| Priority | ID | Assessment | Why |
|---:|---|---|---|
| 1 | **h27** | [`ASSESS-DEMO-h27`](ASSESS-DEMO-h27.md) | Store-and-forward timed sketch + replay |
| 2 | **h26** | [`ASSESS-DEMO-h26`](ASSESS-DEMO-h26.md) | Live ink raster (not product protocol) |
| 3 | **h28** | [`ASSESS-DEMO-h28`](ASSESS-DEMO-h28.md) | 12 FPS full-frame SPI stress |
| 4 | **h13** | [`ASSESS-DEMO-h13`](ASSESS-DEMO-h13.md) | Server RGB565 preview path |
| 5 | **p12** | [`ASSESS-DEMO-p12`](ASSESS-DEMO-p12.md) | CPU FB + single `lv_image` (anti-tear) |
| 6 | **h23** | [`ASSESS-DEMO-h23`](ASSESS-DEMO-h23.md) | Idle stop upload policy — **no LVGL** |

Pair **h27** with **x02** `v1_record` via [`ASSESS-SHELL-x02`](ASSESS-SHELL-x02.md). Compare h19 record **stub** (no FB) before assuming capture works.

---

## Changelog

| Rev | Date | Notes |
|---|---|---|
| 0.1 | 2026-10-11 | Initial explorer index; 51 demos; ledger rev 0.2 cross-check |
| 0.2 | 2026-10-11 | Flash shortlist linked to static `ASSESS-*` files (9 total) |
| 0.3 | 2026-10-11 | Sketch/media tier + six more `ASSESS-*` files |
