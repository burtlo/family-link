# Family desk link (working title)

A persistent Wi-Fi desk device so each child can send and receive **small photos**, **async audio**, and join a **live hangout** with a parent — without borrowing the other parent’s phone.

The folder name is a placeholder. Name candidates live in [`docs/NAMES.md`](docs/NAMES.md).

This is **not** a continuation of `eink-family-messenger` (canned-phrase ESP32 + e-ink) or `eink-device-landscape` (pocket e-ink Gmail phones). Those answered different questions. Alignment notes: [`docs/NOT-THE-OTHER-REPOS.md`](docs/NOT-THE-OTHER-REPOS.md).

## What it is

| Role | Device |
|---|---|
| Parent | The phone they already carry |
| Each child | A desk box on 2.4 GHz Wi-Fi (ESP32-S3-BOX-3 + dock) |

v1 is a **hangout** (Lynn, Mazi, Arlo, Audrey): users sign in on BOX-3 endpoints, async audio mailbox, web admin. See [`docs/plans/v1-product-spec.md`](docs/plans/v1-product-spec.md).

```bash
make v1-server      # leave running
make demo-v1        # smoke test
make x02            # flash product shell
```

Admin UI: `http://192.168.8.143:8080/app/v1.html` (this Mac’s LAN IP; box twin at `/box/`)

## Start here

1. [`docs/AGENTS.md`](docs/AGENTS.md) — handoff for anyone continuing the work
2. [`docs/OPEN-QUESTIONS.md`](docs/OPEN-QUESTIONS.md) — decisions not yet made
3. [`docs/REQUIREMENTS.md`](docs/REQUIREMENTS.md) — what we actually want
4. [`docs/HARDWARE.md`](docs/HARDWARE.md) — ESP32-S3-BOX-3 specs vs those requirements
5. [`docs/BOX-UI.md`](docs/BOX-UI.md) — child screen, physical buttons, home-screen brief for Stitch
6. [`docs/TRYOUT.md`](docs/TRYOUT.md) — one box + iPhone/Mac/PC
7. [`docs/PHASES.md`](docs/PHASES.md) — 1:1 now, group hangout later
8. [`docs/NAMES.md`](docs/NAMES.md) — project name ideas
9. [`docs/SERVER-DEMOS.md`](docs/SERVER-DEMOS.md) — host protocol demos (Python twins, then the real server)
10. [`docs/DEVICE-DEMOS.md`](docs/DEVICE-DEMOS.md) — BOX-3 feasibility demos (display, PTT, codecs, Wi-Fi, then the same API)
11. [`docs/PERSONALITY-DEMOS.md`](docs/PERSONALITY-DEMOS.md) — face/avatar, motion, button reactions, UI chirps (pet-like presence)
12. [`docs/PERSONA-ASSETS.md`](docs/PERSONA-ASSETS.md) — parent photo + voice → crop / record / pack onto the box (stand-ins until you add media)
13. [`docs/DEMO-MAP.md`](docs/DEMO-MAP.md) — what is built, how audio moves, what to write next
14. [`docs/TLS.md`](docs/TLS.md) — HTTPS for iPhone getUserMedia and firmware h16

## Status

**Hardware:** one BOX-3 is on the desk and enumerates on this Mac as `/dev/cu.usbmodem*` (2026-08-20).

**Software:** island protocol demos **01–06**, combined glue host (`make demo-combined`, `/app` parent page), BOX-3 firmware **h01–h28** / **x01** / **p01–p12**, and persona packing. Map: [`docs/DEMO-MAP.md`](docs/DEMO-MAP.md). Two-box Mazi/Arlo line: [`docs/TWO-BOX.md`](docs/TWO-BOX.md). Replace stock wake-word firmware before the box leaves this house.
