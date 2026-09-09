# Open questions

Decided items stay in [`REQUIREMENTS.md`](REQUIREMENTS.md). This file is only what was never locked. Do not pick these silently in a way that is hard to undo (public hosting, storing kids’ audio forever, wake word).

## Product

- **Name.** Shortlist in [`NAMES.md`](NAMES.md). Folder is still `family-link`.
- **Children’s display names / device IDs.** Two children; exact names not recorded here on purpose. Ask before putting them on a lock screen.
- **Can kids type text** on the 320×240 touchscreen, or is outbound **audio-only** until a keyboard exists?
- **Child → parent photos.** Deferred — out of v1 merge per [`plans/v1-product-spec.md`](docs/plans/v1-product-spec.md).
- **PIN model.** Resolved — see [`plans/v1-product-spec.md`](plans/v1-product-spec.md): per-user PIN; Lynn resets via `/app`; 1 min relock; web creds separate from box PIN.
- **Max clip length.** Resolved for v1 merge: **3 min** cap, **5s** silence auto-stop, long-press stop, 150ms trim.
- **Retention.** How long are voicemails kept? Can a user or Lynn delete?
- **Failed outbound upload.** Retry from a local outbox? Show “not sent yet” on idle? See [`STORAGE.md`](STORAGE.md).
- **Quiet hours / volume.** Household at the other house; 1 W speaker next to a Switch. Hardware options: [`SPEAKER.md`](SPEAKER.md).
- **Ages.** Affects PIN, whether they can type, whether Minecraft PTT must be an arcade button.
- **How much “pet” on the LCD.** Locked idle is already count-only for *content*. A face + badge is compatible; how lively (blink rate, glow, chirps) in the other house is not decided. Demo plan: [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md).
- **Character look / name.** v1 uses **geometry (slot 0) + 12 built-in fun creature avatars**; user picks on shoulder settings. `/app` upload gallery is phase 2. See [`plans/carousel-ui-refresh.md`](plans/carousel-ui-refresh.md).
- **SFX vs quiet hours.** Chirps on press/new-mail vs a silent face. Demos include a mute-SFX hook; household rule is still open.

## Privacy and the other house

- **Always-on box with a mic** (even if button-gated). What was agreed with the other parent besides “leave it plugged in”?
- **Wake word is forbidden** for v1. Confirm nobody re-enables ESP-SR “for convenience.”
- **Who can walk up and record** if there is no outbound PIN? Resolved: **PIN required for recording** in v1.
- **Encryption / where audio lives.** Home server disk vs VPS vs iCloud-adjacent. Kids’ voices.

## Server and network (blocks “box at their house”)

Tryout on **this Mac’s LAN** is easy. Production is not:

- Box is on **their** Wi-Fi. Parent phone is on **cellular or dad’s Wi-Fi**. They must meet on a **reachable HTTPS host**.
- Options for remote boxes: **Tailscale** on home Windows server is the planned path ([`plans/v1-product-spec.md`](plans/v1-product-spec.md)). VPS, Cloudflare Tunnel still alternatives.
- **iPhone push.** PWA push, ntfy, email, SMS, or just open the page?
- **Two SSIDs.** Dev network here vs 2.4 GHz at their house. Provisioning UX (baked list vs setup AP vs USB config).
- Confirm their 2.4 GHz SSID still matches what we were told. 5 GHz-only mesh SSIDs will fail.

## Hangout

- Parent side: **hold-to-talk** as well, or phone is hot-mic while the session is open?
- If **both kids** are in Minecraft before phase 3 mixing exists: two sequential sessions, or wait?
- Session timeout? Who can barge in?
- In-game vs beside-the-game is decided (beside). Still: headphones on the Switch vs box speaker — expected to be messy; measure on hardware. Headset on the **box** (parent voice in cans, boom mic on Pmod) is an option in [`SPEAKER.md`](SPEAKER.md).

## Hardware / firmware

- **Which kit arrived** — BOX-3 full (`-ND`) vs BOX-3B? Same MCU; extras unused.
- **Mute vs red-circle vs Pmod arcade** as the PTT control after a real Minecraft test.
- **Dock always attached** in production (stand + later camera) vs box USB-C only on the desk.
- **ESP-IDF version.** BSP `esp-box-3` wants recent IDF (docs mentioned ≥5.3; confirm current component).
- **Audio codec format** on the wire: Opus vs WAV/PCM vs AMR. Size vs latency vs ESP decode cost. **Minutes-long child→parent diaries strongly favor Opus or chunked PCM** ([`STORAGE.md`](STORAGE.md)).
- **Clock / TLS CA bundle** on the ESP32 for HTTPS.

## Parent client

- Safari PWA vs a small native wrapper. Mic+photos work in Safari with HTTPS.
- One parent only for v1, or a second adult account later?
- **Amazfit 2 watch sidecar.** Custom Zepp OS mini app + Side Service vs phone-only. Opus on watch vs WAV on box. See [`WATCH.md`](WATCH.md). Not started.

## Repo hygiene

- This tree is **docs only**; no git history required yet. Do not copy `eink-family-messenger` into it.
- Secrets (Wi-Fi, PIN, device tokens) never in git.
