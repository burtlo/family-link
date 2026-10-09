# ADR-002: No wake word; mic only while recording

| Field | Value |
|-------|-------|
| **Status** | Accepted |
| **Date** | 2026-09-04 (product spec); indexed 2026-10-08 |
| **Labels** | **Specification** (product); **Observed** (stock BOX ROM behavior) |

## Context

ESP32-S3-BOX-3 ships with Espressif stock firmware: a **wake-word assistant** (“Hi E.S.P.”) with always-on listening when unmuted. The other-house privacy model requires an appliance that does not listen until the user explicitly starts a recording after PIN and recipient selection.

Product conversation locked: no ESP-SR wake word, no always-on mic for v1.

## Decision

1. **No wake word** in v1 product firmware. Do not enable ESP-SR or stock wake-word paths “for convenience.”
2. **Microphone live only during recording** — after the user has passed PIN, chosen a recipient, and recording has started. Not during carousel idle, sleep, or connecting screens.
3. **Replace stock firmware** before deploying a box to the other house. First bring-up may use Espressif BSP `display_audio_photo`, then project demos/product ([`AGENTS.md`](../AGENTS.md), [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md)).
4. **Physical controls:** stock **mute** toggles wake-word; product remaps to **hold-to-talk / hardware mic gate** with on-screen warning when latched ([`plans/v1-product-spec.md`](../plans/v1-product-spec.md) § Physical controls).

Personality demos (p13, etc.) follow the same rule: no listen-until-button ([`PERSONALITY-DEMOS.md`](../PERSONALITY-DEMOS.md)).

## Consequences

- `sdkconfig` / BSP defaults must keep wake-word stacks disabled for product and island demos.
- Peel screen protector on new units — **Observed** ops note: muffled mics if left on ([`AGENTS.md`](../AGENTS.md)).
- Open question remains: social agreement with the other parent about a plugged-in, button-gated mic — see [`open-questions.md`](open-questions.md) § Privacy.

## Evidence and references

- [`REQUIREMENTS.md`](../REQUIREMENTS.md) — “mic live only during recording; no wake word”
- [`SPEAKER.md`](../hardware/SPEAKER.md) — wake-word external mic **forbidden**
- [`DEVICE-DEMOS.md`](../DEVICE-DEMOS.md) — island mic policy
- [`hardware/limitations.md`](../hardware/limitations.md) — stock wake-word ROM
