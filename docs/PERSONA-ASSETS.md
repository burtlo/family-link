# Persona assets — parent likeness pipeline

Host tools that turn a **parent photo + a short voice clip** into files the BOX-3 can show and play. Kids see *you* on the desk, not a mascot and not their own photo.

You do **not** have to drop real photos or recordings in yet. Every step has a generated stand-in. When you are ready, pass `--in` / `--webcam` / `--mic` on the same scripts.

Geometric pet demos (**p01–p09**) stay as they are. This track is **a10–a14** on the Mac and **p10–p11** on the box.

## What this is

A capture → cut → record → ideas → pack pipeline. Packed RGB565 + PCM land in flash. Firmware demos prove the LCD can show a portrait and the speaker can play a greeting that is *your* voice later.

## What this is not

- Not a child’s photo on the lock screen (still forbidden without asking).
- Not a wake word and not live listening. The greeting plays when the demo says so (boot or mute tap). Device mic stays button-gated.
- Not UI chirps. p06 chirps stay non-speech. A parent “hey, it’s me” clip is a **greeting**, a different slot.
- Not committing family media. Personal files are gitignored.

## Privacy

| Path | Git |
|---|---|
| `data/persona/` | ignored (captures, cuts, recordings) |
| `firmware/assets/persona/` | ignored (your packed headers) |
| `firmware/assets/persona.example/` | committed stand-ins only (watermarked SAMPLE art, not-speech tone) |

Do not copy real portraits into `persona.example/`. Pack your likeness with `--dest firmware/assets/persona/` (the ignored overlay). Firmware includes that overlay when present, else the example.

## Host demos (Mac, no box)

```
demos/persona/
  01_capture/capture.py   # still: file / webcam / generated stand-in
  02_cut/cut.py           # square crop, optional --box, desk-contrast
  03_record/record.py     # WAV: file / mic / generated tone; trim + fade
  04_ideas/ideas.py       # stylized variants from one crop (pick a look later)
  05_pack/pack.py         # PNG → LVGL RGB565 C array; WAV → PCM C array
```

Pass: `-- PASS a01` … `a05`. `make demos-persona` runs them in order on fixtures.

When you have media:

```
python demos/persona/01_capture/capture.py --in ~/Photos/desk-dad.jpg
python demos/persona/01_capture/capture.py --webcam          # ffmpeg + avfoundation
python demos/persona/02_cut/cut.py --box 80,40,200,200
python demos/persona/03_record/record.py --in ~/Audio/hey.m4a
python demos/persona/03_record/record.py --mic --seconds 2
python demos/persona/04_ideas/ideas.py
python demos/persona/05_pack/pack.py --dest firmware/assets/persona
make flash DEMO=p10
```

## Firmware

| Demo | Job |
|---|---|
| **p10** | Show packed idle portrait on 320×240 (fallback: geometric face) |
| **p11** | Play packed greeting on mute tap; portrait stays up. Mic off |

## Avatar ideas (04)

One crop, several treatments so “what should I look like on their desk?” is a folder of PNGs, not a debate:

| Variant | Intent |
|---|---|
| `desk` | High contrast, readable from a chair |
| `poster` | Few colors, illustration-ish |
| `circle` | Badge on the teal body color |
| `pixel` | Tiny then nearest-neighbor (toy, not uncanny) |
| `ink` | Gray, hard threshold |
| `warm` | Shift toward the geometric-head palette |

None of these pick the product look. They exist so you can hold the box next to the folder later.

## Reuse later

Same PNG→RGB565 and WAV→PCM helpers for parent→child photos and voicemail playback. Greeting audio is a **named clip in flash**, not TTS and not a wake-word model.
