# Open questions (decisions index)

**Canonical unresolved list** for agents and planners. The legacy file [`OPEN-QUESTIONS.md`](../OPEN-QUESTIONS.md) remains in place with a pointer here; prefer this file for navigation from [`decisions/README.md`](README.md).

**Decided items** are not repeated below. See [`REQUIREMENTS.md`](../REQUIREMENTS.md), [`plans/v1-product-spec.md`](../plans/v1-product-spec.md), and the [decision index](README.md).

---

## Product

| Question | Notes |
|----------|--------|
| **Product name** | Shortlist in [`NAMES.md`](../NAMES.md). Repo folder is still `family-link`. |
| **Children’s display names / device IDs** | Two children; exact names not recorded in docs on purpose. Ask before putting them on lock screens. |
| **Can kids type text** on the 320×240 touchscreen? | Or is outbound **audio-only** until a keyboard exists? Tied to ages and UX. |
| **Retention duration** | Admin preview/cull/trash/restore architecture is decided; automatic retention duration and trash grace period remain open. [`SERVER-MESSAGE-STORAGE.md`](../SERVER-MESSAGE-STORAGE.md). |
| **Quiet hours / volume** | Household at the other house; 1 W speaker next to a Switch. Hardware options: [`SPEAKER.md`](../hardware/SPEAKER.md). |
| **Ages** | Affects PIN, typing, whether Minecraft PTT must be an arcade button. |
| **How much “pet” on the LCD** | Locked idle is count-only for *content*. Face + badge is compatible; liveliness (blink, glow, chirps) in the other house is not decided. [`PERSONALITY-DEMOS.md`](../PERSONALITY-DEMOS.md). |
| **SFX vs quiet hours** | Chirps on press/new-mail vs a silent face. Demos include a mute-SFX hook; household rule is still open. |

### Resolved elsewhere (do not reopen silently)

| Topic | Where locked |
|-------|----------------|
| Child → parent photos | Out of v1 merge — [`plans/v1-product-spec.md`](../plans/v1-product-spec.md) |
| PIN model, gates, relock | Per-user PIN; Lynn resets via `/app`; 1 min relock — v1 product spec |
| Max clip length | 3 min cap, 5 s silence auto-stop, long-press stop, 150 ms trim — v1 product spec |
| Failed outbound upload (target) | Durable local chunk outbox + server acknowledgement — [`LONG-MESSAGE-ARCHITECTURE.md`](../LONG-MESSAGE-ARCHITECTURE.md); **as-built** still volatile on device — [`storage-qualification-status.md`](storage-qualification-status.md), [`hardware/limitations.md`](../hardware/limitations.md) |
| Character look / name | Geometry slot 0 + 12 built-in avatars; `/app` gallery phase 2 — [`plans/carousel-ui-refresh.md`](../plans/carousel-ui-refresh.md) |
| Who can record without PIN | **PIN required for recording** in v1 |

---

## Privacy and the other house

| Question | Notes |
|----------|--------|
| **Always-on box with a mic** (button-gated) | What was agreed with the other parent besides “leave it plugged in”? |
| **Wake word** | **Forbidden** for v1 — [`no-wake-word-mic-only-while-recording.md`](no-wake-word-mic-only-while-recording.md). Confirm nobody re-enables ESP-SR “for convenience.” |
| **Encryption / where audio lives** | Home server disk vs VPS vs iCloud-adjacent. Kids’ voices. |

---

## Server and network (blocks “box at their house”)

Tryout on **this Mac’s LAN** is easy. Production is not:

| Question | Notes |
|----------|--------|
| **Reachable HTTPS host** | Box on their Wi-Fi; parent phone on cellular or other Wi-Fi. |
| **Remote path** | **Tailscale** on home Windows server is the planned path ([`plans/v1-product-spec.md`](../plans/v1-product-spec.md)). VPS, Cloudflare Tunnel still alternatives. |
| **iPhone push** | PWA push, ntfy, email, SMS, or just open the page? |
| **Two SSIDs** | Dev network here vs 2.4 GHz at their house. Provisioning UX (baked list vs setup AP vs USB config). |
| **Their 2.4 GHz SSID** | Confirm it still matches what we were told. 5 GHz-only mesh SSIDs will fail. |

---

## Hangout (live voice — later than v1 merge)

| Question | Notes |
|----------|--------|
| Parent **hold-to-talk** vs phone hot-mic | While session open. |
| **Both kids** in Minecraft before phase 3 mixing | Two sequential sessions, or wait? |
| Session timeout / who can barge in | Undecided. |
| Switch headphones vs box speaker | Beside-the-game is decided; acoustics still messy — measure on hardware. Headset on box option in [`SPEAKER.md`](../hardware/SPEAKER.md). |

---

## Hardware / firmware

| Question | Notes |
|----------|--------|
| **Which kit arrived** | BOX-3 full (`-ND`) vs BOX-3B? Same MCU; extras unused. |
| **Mute vs red-circle vs Pmod arcade** | PTT control after a real Minecraft test. |
| **Dock always attached** | Stand + later camera vs box USB-C only on desk. |
| **ESP-IDF version** | BSP `esp-box-3` wants recent IDF (docs mentioned ≥5.3; confirm current component). |
| **Clock / TLS CA bundle** | HTTPS to real CA on ESP32 without SNTP — **Specification** failure until time sync ([`hardware/limitations.md`](../hardware/limitations.md)). |

---

## Parent client

| Question | Notes |
|----------|--------|
| Safari PWA vs small native wrapper | Mic+photos work in Safari with HTTPS. |
| One parent only for v1 vs second adult later | Undecided. |
| **Amazfit 2 watch sidecar** | Exploratory only; no repo code. Zepp OS mini app vs phone-only. **Abandoned** for v1 — not integrated. |

---

## Repo hygiene

| Question | Notes |
|----------|--------|
| Git history | This tree may be docs-heavy; do not copy `eink-family-messenger` into it. |
| Secrets | Wi-Fi, PIN, device tokens never in git. |
