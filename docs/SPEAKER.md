# Audio I/O (BOX-3)

The onboard cone is **8 Ω / 1 W**. Codec volume in this tree already goes to **100** (`esp_codec_dev` → 0 dB). That is the digital ceiling for the built-in path. Louder playback, **headphones**, and an **external mic** need extra hardware — but they can still hang off this kit (on-board codec pads, or **Pmod GPIOs** on the dock).

Do not rewrite I2S0 / the BSP codecs to “fix volume.” Add a parallel path or tap an existing one.

Section titles below are the names to use in conversation — there are no separate codes like “S1” or “H1.”

## What is already on the box

**Playback**

```
I2S0  -->  ES8311 DAC  -->  NS4150 class-D  -->  8 Ω / 1 W speaker
                              ^
                         PA enable GPIO46
```

**Capture**

```
Dual onboard mics  -->  ES7210 ADC (MIC1 + MIC2)  -->  I2S0 DSIN (GPIO16)
                              ^
                    Top mute latch (GPIO1) can hardware-mute the onboard pair
```

| Piece | Fact |
|---|---|
| Playback DAC | **ES8311** (I2C). Chip includes a **headphone driver**, but the PCB routes output to **NS4150 only** — no jack on the shell. |
| Speaker amp | **NS4150**, enable **GPIO46** |
| Capture ADC | **ES7210**, four analog mic inputs. **MIC1 + MIC2** = front mics. **MIC3** = playback reference (for Espressif AEC demos). **MIC4** is typically **not populated** on BOX-3 — check the schematic before assuming a spare analog pin. |
| Hardware mute | **GPIO1** (`BSP_MUTE_STATUS`). When the top **mute latch is down**, onboard mics are **analog-dead** (see **h17** / **h08**). Firmware still must gate recording on PTT; this is an extra hardware layer. |
| Headphone jack | **None** |
| Mic jack | **None** |
| Dock | **Pmod** = GPIO + 3.3 V only. No analog line-in/out. No I2S broken out. |

I2S0 clocks are owned by the codecs: **SCLK GPIO17**, **LRCK GPIO45**, **MCLK GPIO2**, **DOUT GPIO15**, **DSIN GPIO16**. Those pins are **not** on Pmod.

## What the dock gives you

Sit the box on the **BOX-3-DOCK**. Two **Pmod** headers: **16 GPIOs at 3.3 V**, GND, 3V3. USB-A is **USB 1.1 host** ([`UVC-CAMERA.md`](UVC-CAMERA.md)). Dock USB-C is **5 V in only**.

Reserve **Pmod 1** for a later arcade PTT button. Put audio add-ons on **Pmod 2**.

| Avoid | Why |
|---|---|
| Pmod1 IO2 / IO6 (GPIO20 / 19) | USB D+ / D− (camera path) |
| Pmod1 IO4 / IO8 (GPIO40 / 41) | Dock I2C |
| Pmod2 IO4 / IO8 (GPIO44 / 43) | UART0 |
| **GPIO1** for your own ADC | Already **mute status** for onboard mics |

**Suggested Pmod 2 wiring** for a second I2S port (I2S1). ESP32-S3 has **I2S1**; leave I2S0 on ES8311 + ES7210.

| Signal | Pmod 2 | GPIO | Notes |
|---|---|---|---|
| BCLK | IO3 | **12** | Shared TX + RX |
| LRCLK | IO1 | **13** | Shared |
| I2S **TX** data (out to amp/DAC) | IO7 | **11** | Playback copy |
| I2S **RX** data (in from digital mic) | IO5 | **10** | Optional external mic |
| GND | GND | — | |
| 3.3 V | 3V3 | — | Logic only |

Firmware copies the same 16 kHz s16le mono the ES8311 already plays to I2S1 TX, and (if present) reads I2S1 RX instead of — or mixed with — ES7210. **Software** must still honor PTT / mute latch for any Pmod mic; GPIO1 does **not** gate an I2S mic on the dock.

For louder class-D amps, feed **5 V** from dock VBUS, not from a GPIO.

---

## Louder speaker

Ranked by effort. See **Headphones** and **External microphone** below for the other paths.

| Option | Firmware |
|---|---|
| **Bigger cone (swap driver)** — same **NS4150**, larger **8 Ω** cone (open shell) | None |
| **Pmod class-D speaker (MAX98357A)** + 4–8 Ω speaker | I2S1 TX copy |
| **Pmod line-out to powered speakers (PCM5102A)** → powered 3.5 mm speakers | I2S1 TX copy |

**Bigger cone (swap driver)** is the only drop-in today. The two **Pmod** playback options need a dock + I2S1 hook in firmware.

---

## Headphones

There is no jack. The ES8311 **can** drive headphones, but the board does not bring that out.

### Pmod DAC + headphone amp (PCM5102A) — recommended, no shell surgery

**PCM5102A** on the Pmod pins above → a small **headphone amplifier** → **3.5 mm TRS** (or TRRS socket if you also wire a mic — see **Combined headset** below).

- PCM5102 alone is **line level** (~2 V). Many phones’ earbuds will play quietly; gaming headsets with higher impedance need an amp.
- Use a breakout that combines DAC + headphone amp (search “PCM5102 headphone” / **PAM8302** / **TPA6112** class), or line-out into **powered** USB headphones (their own amp).
- Same I2S1 TX copy as **Pmod line-out to powered speakers**. Optionally drive **GPIO46 low** when headphones are the only output so the onboard cone stays quiet.

**Good for:** Minecraft desk — child hears you in **their** headset while the TV stays on Switch audio.

### ES8311 headphone tap (PCB mod)

Open the unit, find **ES8311 HPOUT** in the [schematic](https://github.com/espressif/esp-box/tree/master/hardware/SCH_ESP32-S3-BOX-3_V1.0), add AC coupling caps + **3.5 mm jack** per Espressif’s reference (typically ~100 µF series caps, jack ground to analog GND).

- Still uses **I2S0 + ES8311** — no I2S1 firmware.
- No automatic speaker/headphone switch. Either disable **GPIO46** in software when “headphones mode” is selected, or accept both cone + cans at low volume.
- Best if you want one clean analog port inside the case.

### Skip for headphones

| Idea | Why not |
|---|---|
| Bluetooth headphones | ESP32-S3 = **BLE only**, no A2DP |
| USB headset on dock USB-A | USB-audio host stack + steals the camera port |
| MAX98357 → headphones | Class-D **speaker** amp; wrong impedance / no isolation for cans |
| Raw PCM5102 → low-impedance cans with no amp | Quiet or distorted; add a **Pmod DAC + headphone amp** |

---

## External microphone

Onboard mics are fine at desk distance but pick up the TV and the speaker. A **boom mic** or **headset mic** beside the child’s mouth is the realistic Minecraft fix ([`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md): Switch headphones vs box speaker).

### Pmod I2S digital mic (INMP441) — recommended, dock only

**INMP441** or **ICS-43434** (~$3–5) on Pmod 2:

| Mic | Connect to |
|---|---|
| VDD | 3.3 V |
| GND | GND |
| SCK | BCLK (GPIO **12**) |
| WS | LRCLK (GPIO **13**) |
| SD | GPIO **10** (I2S1 RX) |
| L/R | GND = left channel |

- **Does not** use ES7210 or `bsp_audio_codec_microphone_init()`. Add a second capture path on **I2S1 RX** at the same 16 kHz s16le the product already uses.
- **PTT / mute:** GPIO1 only mutes the **onboard** pair. External mic must be **software-muted** when the button is up — same rules as **h08** / **h11**, applied in your I2S1 read loop.
- Optional: disable onboard ES7210 reads when the external mic is plugged in (config flag or jack-detect GPIO if you add one).

**Good for:** Clip-on boom mic on the desk, or a modded headset (separate mic wires — see **Combined headset**).

### Electret on ES7210 MIC4 (PCB mod)

If the schematic shows **MIC4** (or an unused ES7210 input) routed to test pads or a spare footprint:

- Wire a **2.2 kΩ biased electret** (standard PC headset mic, or MAX9814 breakout feeding the ES7210 analog input — **not** GPIO1 ADC).
- Reconfigure ES7210 channel map in firmware; stay on **I2S0 / ES7210** so gain and sample rate match today’s demos.
- Onboard **GPIO1 hardware mute** may or may not affect MIC4 — verify on the schematic before relying on it.

**Good for:** One **TRRS CTIA** jack wired inside the box (headphones + mic on one cable). More soldering, one port for the kid.

### USB mic on dock USB-A

Same problems as USB speakers: **USB 1.1 full-speed**, no USB-audio in this tree, port reserved for **camera**. Skip for v1.

### Skip for mics

| Idea | Why not |
|---|---|
| MAX9814 → ESP32 **ADC on GPIO1** | GPIO1 is **BSP_MUTE_STATUS** |
| INMP441 into ES7210 “MIC” pins | INMP441 is **I2S digital**; ES7210 inputs are **analog** |
| Wake-word always-on external mic | **Forbidden** for this product |
| Using external mic without PTT gating | Violates product rules even if hardware mute is off |

---

## Combined headset (TRRS) — Minecraft desk

CTIA wiring (most phone/gaming headsets):

| Ring | Signal |
|---|---|
| Tip | Left audio |
| Ring 1 | Right audio |
| Ring 2 | Ground |
| Sleeve | Mic + bias |

**Playback:** **Pmod DAC + headphone amp** or **ES8311 headphone tap** — headphone amp output to tip + ring 1 (mono duplicate both channels is fine for voice).

**Mic:** Sleeve → electret bias → **Electret on ES7210 MIC4** if you mod the PCB, **or** cut the headset cable and wire the mic element to **Pmod I2S digital mic** (INMP441 replacement / separate boom).

**PTT:** Still the box **red circle** or **mute** / future Pmod arcade button. The headset mic is not push-to-talk unless you add a switch in the cable.

**Echo:** Headphones help — child hears you without cranking the cone toward the TV. Half-duplex hangout (**h11**) still applies: speaker/headphone path muted while they transmit.

---

## What to buy (indicative)

| Goal | Typical parts |
|---|---|
| Louder cone | 8 Ω 3 W 40–50 mm driver (**Bigger cone (swap driver)**) |
| Room speaker | [Adafruit MAX98357A](https://www.adafruit.com/product/3006) + 4–8 Ω speaker (**Pmod class-D speaker**) |
| Headphones | PCM5102A breakout + headphone amp board + 3.5 mm TRS (**Pmod DAC + headphone amp**), **or** powered speakers you already own (**Pmod line-out to powered speakers**) |
| External mic | INMP441 or ICS-43434 breakout + Dupont to Pmod 2 (**Pmod I2S digital mic**) |
| One TRRS cable | CTIA TRRS jack, electret bias parts, schematic time (**Electret on ES7210 MIC4** + **ES8311 headphone tap**) |

---

## Firmware status

| Path | Today |
|---|---|
| ES8311 + ES7210 (onboard) | **h05**, **h08**, **h11**, **h21**, **h25**, **x01** |
| I2S1 TX/RX on Pmod | **Not wired** — needs a small `common/` helper + copy/mux in playback/record |
| GPIO46 off for headphone-only | **Not implemented** — trivial add when I2S1 exists |
| ES7210 MIC4 / TRRS | **Not implemented** — depends on PCB tap |

---

## Related

- Kit facts: [`HARDWARE.md`](HARDWARE.md)
- Codec feasibility: [`DEVICE-DEMOS.md`](DEVICE-DEMOS.md)
- Switch + headset open question: [`OPEN-QUESTIONS.md`](OPEN-QUESTIONS.md)
- Do not rewrite I2S0: [`AGENTS.md`](AGENTS.md)
