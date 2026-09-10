# Notes for future agents

Read this before writing code. Product intent: [`REQUIREMENTS.md`](REQUIREMENTS.md). **Approved v1 contract:** [`plans/v1-product-spec.md`](plans/v1-product-spec.md). Hardware facts: [`HARDWARE.md`](HARDWARE.md). Endpoint screen brief: [`BOX-UI.md`](BOX-UI.md). Removable media: [`STORAGE.md`](STORAGE.md). Unresolved decisions: [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md). Demo plans: [`SERVER-DEMOS.md`](SERVER-DEMOS.md), [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md), [`DEMO-MAP.md`](DEMO-MAP.md), [`plans/v1-demo-set.md`](plans/v1-demo-set.md).

## What this project is

A **desk answering machine** for a hangout (Lynn, Mazi, Arlo, Audrey): users sign in on BOX-3 **endpoints**, async audio mailbox, web admin on the home server.

- Lynn uses a **BOX-3** (parity) plus **web `/app`** on the Windows server.
- Each person can use **any endpoint** after PIN sign-in.
- Media in v1 merge: **async audio** only. Live voice, drawing, photos: later.
- **No video. No cellular. No e-ink. No canned phrases. No wake word.**

Folder name `family-link` is a placeholder. Names: [`NAMES.md`](NAMES.md). Do not brand around split-household language or Marco Polo.

## What this project is not

| Tree | Do not reuse |
|---|---|
| `/Users/lynnfrank/eink-family-messenger` | Arduino Waveshare firmware, five buttons, matching boxes, 5 s poll |
| `/Users/lynnfrank/src/eink-device-landscape` | T-Deck / HiBreak / Gmail / US-LTE shopping |

Reuse at most: “server you own” and “device identity.”

## Hardware on the desk (as of 2026-08-20)

A BOX-3 (or 3B — same main unit) is **plugged into this Mac**. Native USB serial is typically:

```
/dev/cu.usbmodem113401
```

(USB modem numbers move when you replug. `ls /dev/cu.usbmodem*`. Two-box flashes remember the USB **serial** in `kits.local.yaml` so the kit can change jacks.)

**Flash through USB-C on the box**, not the dock’s USB-C (dock USB-C is **power only**). Unplug any USB camera on the dock USB-A while flashing. If download fails: hold **Boot**, tap **Reset**, release Boot.

Peel the **screen protector** or the mics are muffled (Espressif).

Stock firmware is a **wake-word assistant** (“Hi E.S.P.”). Replace it before the box goes to the other house. First bring-up flash should be Espressif BSP example `display_audio_photo` (`espressif/esp-box-3`), then ours.

Mute (top) in stock firmware toggles wake-word. **Remap to hold-to-talk.** Red circle under the screen is extra touch.

SSID and password are **known** and may be baked in. Never commit them. NVS / `sdkconfig` local overlay / secret file in `.gitignore`.

## Architecture (do not invert)

```
endpoint  --HTTPS/WSS-->  server (Windows, home LAN)  <--  web /app
```

v1: **three endpoints**, four users, async audio. Lynn’s server at home; kids’ boxes on remote Wi-Fi when deployed (TLS + Tailscale before ship). Product glue: **x02** (successor to x01) per [`plans/v1-product-spec.md`](plans/v1-product-spec.md).

Child → parent photos need a **UVC/MJPEG USB 1.1** camera on dock USB-A. Parent → child photos do not (phone camera roll, server downscales to ~320×240).

Nintendo Switch Online cannot run on the box. Voice sits **beside** Minecraft.

## Suggested build order

Most island/protocol/personality demos **and the first product glue** are in the tree. See [`DEMO-MAP.md`](DEMO-MAP.md).

- Combined host: `python -m demos.server.combined.server --host 0.0.0.0 --port 8080` (parent page at `/app/`).
- Box LCD twin (no flash): `python demos/server/h18_playback/server.py --host 0.0.0.0 --port 8080` then `http://localhost:8080/box/` (320×240 + bezel; same h18 catalog).
- Product box: `make x02` / `make flash DEMO=x02` (v1 shell). Logic lives in **`firmware/v1/`** (`x02_main.c` + modules); `firmware/demos/x02_product_shell.c` is a one-line flash shim (`flash.py` maps demo id **`x02`** → `x02_product_shell`). After editing timing: `make v1-timing`; verify parity: `make check-v1-parity`. Island demos remain **h01–h28**. Build order: [`plans/v1-product-spec.md`](plans/v1-product-spec.md).
- Island demos (h20–h27) prove sync paths for **later**; not in v1 merge.
- Remote endpoints need TLS: [`TLS.md`](TLS.md) / Tailscale on home server.

## Constraints that are already decided

- Wi-Fi 2.4 GHz only. No 5 GHz, no SIM.
- Mic live **only during recording** (after recipient pick). No wake word.
- PIN gates **carousel and recording**. 1 min idle relock.
- Lynn client: BOX-3 endpoint + web `/app`, not phone-primary.
- USB wall power; desk appliance.
- v1 merge: **async audio only** — see [`plans/v1-product-spec.md`](plans/v1-product-spec.md).

## Stability and device verification

Stability analysis (INT-001–INT-015, anti-patterns, prioritized fixes): [`../stability-synthesis.md`](../stability-synthesis.md).

**Cursor rules** (`.cursor/rules/`):

| Rule | Scope |
|------|-------|
| [`device-verify-before-done.mdc`](../.cursor/rules/device-verify-before-done.mdc) | Firmware + web twin — serial log or user confirmation before task close |
| [`async-state-no-stale-gates.mdc`](../.cursor/rules/async-state-no-stale-gates.mdc) | x02 / v1 async workers — no silent `continue` without UI reconciliation |
| [`lvgl-incremental-ui.mdc`](../.cursor/rules/lvgl-incremental-ui.mdc) | x02, p13, v1 UI — incremental updates; 64 KB LVGL heap budget |
| [`v1-auth-scope-freeze.mdc`](../.cursor/rules/v1-auth-scope-freeze.mdc) | INT-014 class — no carousel/feature edits while debugging PIN/connectivity |

**Project skills** (`.cursor/skills/`):

| Skill | Purpose |
|-------|---------|
| [`device-test-after-flash`](../.cursor/skills/device-test-after-flash/SKILL.md) | Post-flash checklist: WiFi, PIN, carousel; COM4 (Windows) or `/dev/cu.usbmodem*` (Mac) |
| [`web-firmware-parity-check`](../.cursor/skills/web-firmware-parity-check/SKILL.md) | Compare `shared/v1/timing.yaml` → `firmware/v1/v1_timing.h` + `web/v1_timing.js`; run `make check-v1-parity` |

**INT-014 acceptance** (PIN stuck on `checking...`): per [`BOX-UI.md`](BOX-UI.md) line 313 — never infinite checking; network failure must transition to **connecting**, not a frozen pad. Auth logic: `firmware/v1/v1_auth.c` (`login_task_fn`, login generation token). Isolation plan: [`plans/v1-isolation-plan.md`](plans/v1-isolation-plan.md).

## If you are stuck

Open questions are listed in [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md). Do not invent canned phrases, e-ink, or a matching parent gadget to “make progress.”
