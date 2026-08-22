# Open questions

Decided items stay in [`REQUIREMENTS.md`](REQUIREMENTS.md). This file is only what was never locked. Do not pick these silently in a way that is hard to undo (public hosting, storing kids’ audio forever, wake word).

## Product

- **Name.** Shortlist in [`NAMES.md`](NAMES.md). Folder is still `family-link`.
- **Children’s display names / device IDs.** Two children; exact names not recorded here on purpose. Ask before putting them on a lock screen.
- **Can kids type text** on the 320×240 touchscreen, or is outbound **audio-only** until a keyboard exists?
- **Child → parent photos.** USB webcam on the dock in v1, or defer until audio+hangout work?
- **PIN model.** Who sets it? Does the parent know it? Recovery if forgotten? Timeout length?
- **Retention.** How long are voicemails and photos kept? Can a child or parent delete?
- **Quiet hours / volume.** Household at the other house; 1 W speaker next to a Switch.
- **Ages.** Affects PIN, whether they can type, whether Minecraft PTT must be an arcade button.
- **How much “pet” on the LCD.** Locked idle is already count-only for *content*. A face + badge is compatible; how lively (blink rate, glow, chirps) in the other house is not decided. Demo plan: [`PERSONALITY-DEMOS.md`](PERSONALITY-DEMOS.md).
- **Character look / name.** Placeholder geometry in p01–p09. Parent likeness pipeline: [`PERSONA-ASSETS.md`](PERSONA-ASSETS.md) (photo crop + greeting WAV). Do not pick a mascot or put a **child’s** photo on the lock screen without asking. Which packed variant (photo / poster / geometric) ships is still open.
- **SFX vs quiet hours.** Chirps on press/new-mail vs a silent face. Demos include a mute-SFX hook; household rule is still open.

## Privacy and the other house

- **Always-on box with a mic** (even if button-gated). What was agreed with the other parent besides “leave it plugged in”?
- **Wake word is forbidden** for v1. Confirm nobody re-enables ESP-SR “for convenience.”
- **Who can walk up and record** if there is no outbound PIN? Sibling prank clips.
- **Encryption / where audio lives.** Home server disk vs VPS vs iCloud-adjacent. Kids’ voices.

## Server and network (blocks “box at their house”)

Tryout on **this Mac’s LAN** is easy. Production is not:

- Box is on **their** Wi-Fi. Parent phone is on **cellular or dad’s Wi-Fi**. They must meet on a **reachable HTTPS host**.
- Options never chosen: VPS, Tailscale/Headscale, Cloudflare Tunnel, always-on Mac/Pi at dad’s with port forward, ntfy + polling only.
- **iPhone push.** PWA push, ntfy, email, SMS, or just open the page?
- **Two SSIDs.** Dev network here vs 2.4 GHz at their house. Provisioning UX (baked list vs setup AP vs USB config).
- Confirm their 2.4 GHz SSID still matches what we were told. 5 GHz-only mesh SSIDs will fail.

## Hangout

- Parent side: **hold-to-talk** as well, or phone is hot-mic while the session is open?
- If **both kids** are in Minecraft before phase 3 mixing exists: two sequential sessions, or wait?
- Session timeout? Who can barge in?
- In-game vs beside-the-game is decided (beside). Still: headphones on the Switch vs box speaker — expected to be messy; measure on hardware.

## Hardware / firmware

- **Which kit arrived** — BOX-3 full (`-ND`) vs BOX-3B? Same MCU; extras unused.
- **Mute vs red-circle vs Pmod arcade** as the PTT control after a real Minecraft test.
- **Dock always attached** in production (stand + later camera) vs box USB-C only on the desk.
- **ESP-IDF version.** BSP `esp-box-3` wants recent IDF (docs mentioned ≥5.3; confirm current component).
- **Audio codec format** on the wire: Opus vs WAV/PCM vs AMR. Size vs latency vs ESP decode cost.
- **Clock / TLS CA bundle** on the ESP32 for HTTPS.

## Parent client

- Safari PWA vs a small native wrapper. Mic+photos work in Safari with HTTPS.
- One parent only for v1, or a second adult account later?

## Repo hygiene

- This tree is **docs only**; no git history required yet. Do not copy `eink-family-messenger` into it.
- Secrets (Wi-Fi, PIN, device tokens) never in git.
