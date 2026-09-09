# Plan: Collect v1 product demos

| Field                          | Value                                                                 |
|--------------------------------|-----------------------------------------------------------------------|
| **Doc kind**                   | `feature-plan` (demo runbook + merge phases)                          |
| **Owners / areas**             | Device firmware, host protocol, web admin                             |
| **Status**                     | `active`                                                              |
| **Targets**                    | Three endpoints; async audio mailbox — see [`v1-product-spec.md`](v1-product-spec.md) |
| **Last updated**               | 2026-09-05                                                            |
| **Supersedes / superseded by** | Product contract: [`v1-product-spec.md`](v1-product-spec.md)        |
| **As-built**                   | None — link to [`docs/features/`](../features/_template.md) when shipped |

## At a glance

**Approved product contract:** [`v1-product-spec.md`](v1-product-spec.md) (2026-09-04). A hangout has users and endpoints; async audio carousel + broadcast; Lynn’s box + web admin; island demos are helpers to import into **x02** (successor to x01).

This file is the **demo runbook** and **merge checklist**. Use [Invoke later](#invoke-later) to re-flash a single island demo.

| Phase | Outcome | Status |
|------------------------------------------------------------|------------------------------|--------|
| [Phase 1 — Starter list from the desk](#phase-1--starter-list-from-the-desk) | Record, buttons, playback screen, and inbox list are named as the seed of the v1 set | `done` |
| [Phase 2 — Collect the rest of the existing demos](#phase-2--collect-the-rest-of-the-existing-demos) | Every v1 job has an existing demo (or an explicit gap) on the list | `todo` |
| [Phase 3 — Fill gaps with new demos](#phase-3--fill-gaps-with-new-demos) | Missing first-version jobs get their own one-job demos before product glue | `todo` |
| [Phase 4 — Product merge](#phase-4--product-merge) | Import helpers into x02 per product spec | `done` |

---

## Background

Demos stay one job per flash. They are not the product binary. When we later bring pieces together (the product shell started as **x01**; v1 ships as **x02** per [`v1-product-spec.md`](v1-product-spec.md)), we import the helper that passed — we do not merge every `.c` file.

This week’s desk work added two screens that the product still lacked: a real playback UI and a scrollable inbox with screen transitions. Those sit next to two make tasks already used to explore the feature set: record-and-send, and live button state with chirps.

**Related docs:** [`REQUIREMENTS.md`](../REQUIREMENTS.md), [`PHASES.md`](../PHASES.md) (phase 1: one-to-one desk), [`DEMO-MAP.md`](../DEMO-MAP.md), [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md), [`SERVER-DEMOS.md`](../SERVER-DEMOS.md).

Flash through **USB-C on the box**, not the dock. Secrets live in `firmware/secrets.h` (gitignored). Host must bind `0.0.0.0` so the box can reach this Mac.

---

## Invoke later

Copy-paste runbook. Each entry has a title, what it proves, and the commands to start any host and flash the kit. Run from the repo root. One demo at a time on the box.

### Shared helpers

```bash
# Serial after a flash (optional)
make monitor

# Build only (no flash)
make build-firmware DEMO=h18
```

`make demo-*` smoke-tests start a host, run a client, then **exit**. For desk exploration that needs a live host, leave the `python … --host 0.0.0.0 --port 8080` process running in another terminal.

---

### h08 — Record and upload voice note

**Proves:** Hold the red circle → record → `POST` WAV; server stores the clip (child → you voicemail path).

**Host (leave running):**

```bash
python -m demos.server.03_messages.server --host 0.0.0.0 --port 8080
```

**Smoke only (starts and stops):**

```bash
make demo-messages
```

**Flash:**

```bash
make h08
# or: make flash DEMO=h08
```

**On the kit:** Hold the red circle to record; release to upload. Watch the host log for `POST /v1/messages`. UART `-- PASS h08` when upload succeeds.

---

### h17 — Button panel, mute, and chirps

**Proves:** Mute / Boot / red circle down-up, analog mic mute gate, speaker chirps on press (circle also on release). Kit only — no host.

**Flash:**

```bash
make h17
# or: make flash DEMO=h17
```

**On the kit:** Press mute, Boot (GPIO0), and the red circle. Mute banner should match the red LED. Chirps on press; circle chirps on press and release. UART `-- PASS h17` after a chirp and both muted / not-muted states.

---

### h18 — Audio message playback screen

**Proves:** Message object (sender, time, url, length, start position, read) → player UI → stream WAV with play/pause, progress, ROOMVOL slider. Boot cycles the clip catalog.

**Host (leave running — required for the box):**

```bash
python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080
```

**Smoke only (starts and stops):**

```bash
make demo-playback
```

**Flash:**

```bash
make h18
# or: make flash DEMO=h18
```

**On the kit:** Wait for the playback screen (`-- PASS h18` after JSON paints). Raise volume from mute (first on-notch is 78). Tap Play. Press Boot to advance clips; list wraps. Catalog is the generated melody plus `voice-05` … `voice-22` in `demos/server/h18_playback/assets/`.

**Without the kit:** open `http://localhost:8080/box/` — same catalog in a 320×240 web twin (Boot = B / N / →).

---

### h19 — Message list and detail transition

**Proves:** Scrollable inbox rows → tap detail → Back to list. Same message record shape as h18. Kit only — no host. Detail Play only nudges the bar (no codec).

**Flash:**

```bash
make h19
# or: make flash DEMO=h19
```

**On the kit:** Scroll the list, tap a row, tap Back. UART `-- PASS h19` after one list→detail and one Back.

---

### h20 — Mute as open/away (Mazi ↔ Arlo)

**Proves:** Mute latch heartbeats available/unavailable; the friend’s next heartbeat sees the new state.

**Host (leave running):**

```bash
python -m demos.server.h20_presence.server --host 0.0.0.0 --port 8080
```

**Smoke only (starts and stops):**

```bash
make demo-presence
```

**Flash** (Mazi = `box-a`, Arlo = `box-b` in that kit’s `secrets.h`):

```bash
make h20
# or: make flash DEMO=h20 PORT=/dev/cu.usbmodem…
```

**On the kit:** Start muted (red LED on). Unmute; the other box should flip from away → open within about a second. UART `-- PASS h20` after the first good heartbeat.

---

### h21 — Live talk while unmuted

**Proves:** Unmute streams PCM through the server both ways (no floor). Mute stops sending; you can still hear the friend.

**Desk success 2026-08-24:** two BOX-3 kits communicate (Mazi ↔ Arlo). Next tryout: same-time two-way with Audrey.

**Host (leave running):**

```bash
python -m demos.server.h21_talk.server --host 0.0.0.0 --port 8080
```

**Smoke only:**

```bash
make demo-talk
```

**Flash** (Mazi = `box-a`, Arlo = `box-b`; both plugged in needs `PORT=`):

```bash
make flash DEMO=h21 WHO=mazi PORT=/dev/cu.usbmodem1101
make flash DEMO=h21 WHO=arlo PORT=/dev/cu.usbmodem101
```

**On the kit:** Start muted. Friend card should follow their mute (muted / talking), not stay on waiting. Raise volume if needed (mute, then 78 … 100). Unmute in different rooms (same desk will echo). UART `-- PASS h21` after ~10 frames each way.

---

### h22 — Diary journal while unmuted

**Proves:** Unmute records continuously and POSTs ~1 s WAV chunks; the server stamps UTC. Mute stops uploads.

**Host (leave running):**

```bash
python -m demos.server.h22_diary.server --host 0.0.0.0 --port 8080
```

**Smoke only:**

```bash
make demo-diary
```

**Flash:**

```bash
make h22
```

**On the kit:** Unmute; watch the host log and `data/h22_diary/`. UART `-- PASS h22` after the first 200. Mute; no new files.

---

### h26 — Shared drawing (two boxes)

**Proves:** Touch coordinates on one BOX-3 are copied through the server and inked on the other. Draw a heart on Mazi; it appears on Arlo.

**Host (leave running):**

```bash
python -m demos.server.h26_draw.server --host 0.0.0.0 --port 8080
```

**Smoke only:**

```bash
make demo-draw
```

**Flash** (Mazi = `box-a`, Arlo = `box-b`; both plugged in needs `PORT=`):

```bash
make flash DEMO=h26 WHO=mazi PORT=/dev/cu.usbmodem1101
make flash DEMO=h26 WHO=arlo PORT=/dev/cu.usbmodem101
```

**On the kit:** Both screens are black. Draw with a finger. Friend’s ink is blue; yours is white. Boot clears both. UART `-- PASS h26` on the kit that first receives a stroke.

**Desk success 2026-08-28. High impact.**

---

### h27 — Drawing note (two boxes)

**Proves:** A timed drawing is stored on the server and replayed later at the same speed. Draw on Mazi, send with Boot; Arlo opens it from **new message**.

**Host (leave running):**

```bash
python -m demos.server.h27_sketch.server --host 0.0.0.0 --port 8080
```

**Smoke only:**

```bash
make demo-sketch
```

**Flash** (Mazi = `box-a`, Arlo = `box-b`):

```bash
make flash DEMO=h27 WHO=mazi PORT=/dev/cu.usbmodem1101
make flash DEMO=h27 WHO=arlo PORT=/dev/cu.usbmodem101
```

**On the kit:** Home has **draw**. Record, then Boot to send. The other kit shows **new message**. Tap to watch; Boot stops. UART `-- PASS h27` on send or first inbound point.

**Desk success 2026-08-28. High impact.**

---

### h28 — Short clip on the LCD

**Proves:** The 320×240 SPI LCD can play a ~5 s moving picture at ~12 fps. Baked RGB565 film (title / bouncing ball / end), no JPEG, no Wi-Fi. Product v1 is still not video.

**Flash:**

```bash
make h28
# or: make flash DEMO=h28
```

**On the kit:** Watch **CLIP** → ball over hills → **END**, then it loops. UART `-- PASS h28` after the first playthrough.

---

### x02 — v1 product shell (roster / PIN / carousel / record)

**Proves:** `GET /v1/hangout`, `POST /v1/session/login`, carousel playback, record upload, sleep/wake backlight, upload toasts.

**Host (leave running):**

```bash
make v1-server
# or: make v1-server-tls   # remote / TLS desk test
```

**Smoke only:**

```bash
make demo-v1
```

**Flash** (`secrets.h` token must match `hangout.example.yaml` endpoint):

```bash
make x02
```

**On the kit:** Tap a user, enter PIN from `hangout.example.yaml`, carousel shows First Message. Long-press red circle → recipient → record (5s silence / 3min cap). Play/Pause, drag timeline to scrub, volume. Peek edges animate (h19 260 ms). Inbox refreshes over WebSocket `{type:"inbox"}` without sign-out. Touch or short circle tap wakes from sleep (2min dim → 5min off). UART `-- PASS x02` after login.

**Web admin:** `http://192.168.8.143:8080/app/v1.html` (Lynn web password in `hangout.example.yaml`).

**TLS desk:** `make v1-server-tls` + `DEMO_SERVER_TLS 1` / `DEMO_SERVER_PORT 8443` / `DEMO_TLS_SKIP_VERIFY 1` in `secrets.h`. See [`REMOTE.md`](../REMOTE.md).

---

### Phase 2 candidates (commands ready; confirm on the Phase 2 pass)

These already exist. Commands are here so you can re-explore without hunting the Makefile. Promote or drop them when Phase 2 is done.

#### h02 — Display and idle count

```bash
make h02
```

#### h03 — Touch PIN pad

```bash
make h03
```

#### h04 — Mute as hold-to-talk

```bash
make h04
```

#### h05 — Mic → speaker loopback

```bash
make h05
```

#### h06 — Wi-Fi join

```bash
make h06
```

#### h07 — HTTP device identity (`GET /v1/me`)

```bash
# Host leave running, or smoke:
python -m demos.server.01_auth.server --host 0.0.0.0 --port 8080
# make demo-auth
make h07
```

#### h09 — Download blob and play (no player UI)

```bash
python -m demos.server.03_messages.server --host 0.0.0.0 --port 8080
# Seed an audio message into the box inbox from the Mac, then:
make h09
```

#### h10 — Playhead after power loss

```bash
# Host with playhead semantics (server demo 04); smoke:
# make demo-cursor
make h10
```

#### h11 — Hangout PTT through the server

```bash
# Prefer the glue host for desk tryout:
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
# Or island protocol: make demo-relay / make demo-hangout
make h11
# Parent twin (optional):
# python demos/parent/live_ptt.py --base-url http://MAC_LAN_IP:8080
```

#### h12 — Live screen while streaming

```bash
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
make h12
```

#### h13 — Show inbound photo preview

```bash
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
make h13
```

#### h14 — Heartbeat and inbox WebSocket

```bash
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
# make demo-heartbeat
make h14
```

#### h15 — Text after PIN

```bash
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
make h15
```

#### h16 — HTTPS auth (LAN skip-verify)

```bash
python scripts/dev_https.py
# Set DEMO_SERVER_PORT to the HTTPS port in secrets for this flash only.
make h16
```

#### x01 — Product shell (locked / PIN / inbox / record / hangout)

```bash
python -m demos.server.combined.server --host 0.0.0.0 --port 8080
# Parent page: open http://MAC_LAN_IP:8080/app/
make x01
```

#### Host-only protocol smokes (no box)

```bash
make demo-auth
make demo-heartbeat
make demo-messages
make demo-cursor
make demo-hangout
make demo-relay
make demo-playback
make demo-presence
make demo-talk
make demo-diary
make demo-combined
make demos-server
```

#### Persona / face (look, not the answering-machine core)

```bash
make demos-persona
make p08    # notice / count-style idle
make p10    # packed portrait
make p11    # packed greeting
```

---

## Phase 1 — Starter list from the desk

**Goal.** The demos just proven on the kit are written down as the seed of the first-version set, with the make tasks that run them.

**Deliverables**

- This plan names the starter four and records full invoke commands under [Invoke later](#invoke-later).
- Run them as separate flashes; do not fold them into **x01** in this phase.
- Keep [`DEMO-MAP.md`](../DEMO-MAP.md) / [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md) as the how-to; this list is which ones matter for v1.

| Title | Id | Host | Flash |
|---|---|---|---|
| Record and upload voice note | h08 | `03_messages` leave running | `make h08` |
| Button panel, mute, and chirps | h17 | none | `make h17` |
| Audio message playback screen | h18 | `h18_playback` leave running | `make h18` |
| Message list and detail transition | h19 | none | `make h19` |

**Acceptance**

- A later agent (or you) can flash those four from this plan without hunting the thread.
- Playback and list stay separate from protocol demo **03** (inbox seq/blob). h18 is a UI fixture.

**Status:** `done`

---

## Phase 2 — Collect the rest of the existing demos

**Goal.** The list includes every existing demo that already proves a first-version job, and marks jobs that still have no demo.

**Deliverables**

- Extend the collection after a pass through [`DEMO-MAP.md`](../DEMO-MAP.md) “Product features vs demos.”
- Desk-confirm only when the map is stale; do not re-flash everything by default.
- Commands for candidates already live under [Invoke later](#invoke-later); promote rows into the starter-style table when confirmed.
- Do not start new firmware in this phase.

Candidates already in the tree (add, drop, or annotate on the pass):

| Product job | Existing demos |
|---|---|
| Locked idle + “N new” | h02, p08, x01 |
| PIN then content | h03, h15, x01 |
| Hold-to-talk mic path | h04, h05 |
| Join the house Wi-Fi | h06 |
| Device identity on the server | h07, `make demo-auth` |
| Play a downloaded clip (no player UI) | h09 |
| Playhead survives a reset (server, not the box) | h10, `make demo-cursor` |
| Live hangout you ↔ box | h11, h12, `make demo-hangout`, `make demo-relay`, parent `/app` |
| Photo you → child | h13, combined `/preview` |
| “Reachable” + new-mail without polling | h14, `make demo-heartbeat` |
| HTTPS from the box (LAN skip-verify) | h16 |
| Glue host + parent page | `make demo-combined` / combined leave running |
| First product-shaped binary | x01 |
| Face / greeting (look, not the answering machine) | p01–p11, `make demos-persona` |

**Acceptance**

- Each first-version job in [`REQUIREMENTS.md`](../REQUIREMENTS.md) (async voice both ways, text, photo you → child, PIN, locked count, PTT hangout) either points at a demo on this list or is called out as a Phase 3 gap.
- Child → you photos and group hangout stay out of v1 unless this pass explicitly promotes them.

**Status:** `todo`

---

## Phase 3 — Fill gaps with new demos

**Goal.** Jobs the first version still needs, and that no current demo proves, get their own one-job demos before anyone imports them into the product shell.

**Deliverables**

- New demos only for gaps left by Phase 2. Same rules as today: one job, UART `-- PASS`, BSP codecs, mic live only while held.
- Likely gaps from the map (confirm in Phase 2 before writing code): upload retry / “sent” feedback; trim the record-start click; quiet hours / dim as a household rule; iPhone mic over HTTPS; folding h18’s player and h19’s list into **x01** as imported helpers.
- Each new demo gets a row under [Invoke later](#invoke-later) and a section in [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md) or [`SERVER-DEMOS.md`](../SERVER-DEMOS.md).

**Acceptance**

- No first-version job is “we will figure it out in the product binary.”
- Glue (x01 or a successor) still imports passing helpers; it does not become the place new jobs are invented.

**Status:** `todo`

---

## Phase 4 — Product merge

**Goal.** Ship the approved product in [`v1-product-spec.md`](v1-product-spec.md): hangout / user / endpoint server, carousel firmware, web admin.

**Deliverables**

| Order | Work | Demo helpers |
|---|---|---|
| 1 | Server: users, hangouts, endpoints, per-user inbox, broadcast, First Message, web auth | Extend `combined/` |
| 2 | Session shell: roster, PIN, idle lock, sign out, sleep / wake | h03 |
| 3 | Carousel: peeks, play / pause, scrub, server position sync | h18, h19 animation |
| 4 | Record: recipient picker, silence-stop, trim, mute, chirps | h08, h17 |
| 5 | Web `/app`: welcome upload, send, PIN reset | combined `/app/` |
| 6 | Remote: TLS + Tailscale before kids’ boxes ship | h16, [`TLS.md`](../TLS.md) |

**Acceptance**

- Three users can sign in on any endpoint, carousel audio, 1:1 and broadcast send, First Message on empty chain.
- Lynn admin can reset PIN and upload welcome audio from `/app`.
- Live voice, drawing, photos **not** required for this phase.

**Status:** `done` (desk 2026-09-05) — LAN sign-off: both kits, sign-in, First Message, play/scrub, 1:1 + broadcast record, cross-box WS inbox, Lynn `/app/v1.html` (PIN reset, welcome, send). **Deferred:** carousel/formatting polish (user notes; not blocking merge). **Still open (functionality):** firmware TLS desk test (`DEMO_SERVER_TLS 1` + `make v1-server-tls`) before remote ship; Tailscale per [`REMOTE.md`](../REMOTE.md).

---

## Open questions

Moved to [`v1-product-spec.md`](v1-product-spec.md) (resolved) and [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md) (still open: retention, quiet hours, remote hosting details).

---

## References

- Firmware: `firmware/demos/h08_record_upload.c`, `h17_button_panel.c`, `h18_playback_screen.c`, `h19_message_list.c`, `x02_product_shell.c`
- Hosts: `demos/server/v1_product/`, `demos/server/03_messages/`, `demos/server/h18_playback/`, `demos/server/combined/`
- Make: `make x02`, `make v1-server`, `make demo-v1`, `make h08`, `make h17`, `make h18`, `make h19`
- Product spec: [`v1-product-spec.md`](v1-product-spec.md)
- Docs: [`REQUIREMENTS.md`](../REQUIREMENTS.md), [`BOX-UI.md`](../BOX-UI.md), [`DEMO-MAP.md`](../DEMO-MAP.md), [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md)
