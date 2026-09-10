---
name: device-test-after-flash
description: Run post-flash device verification on BOX-3 (PIN, carousel, WiFi, connectivity). Use after make flash DEMO=x02 or any firmware change affecting PIN, carousel, WiFi, or connectivity.
disable-model-invocation: true
---

# Device test after flash

## When to use

After `make flash DEMO=x02` or any firmware change affecting PIN, carousel, WiFi, or connectivity.

## Flash with monitor

**Windows (typical):**

```bash
python scripts/flash.py --demo x02 --port COM4 --monitor
```

**Mac (USB modem number varies — replug changes it):**

```bash
ls /dev/cu.usbmodem*
python scripts/flash.py --demo x02 --port /dev/cu.usbmodem113401 --monitor
```

Adjust port to match your machine (`COM4` on Windows; `/dev/cu.usbmodem*` on Mac).

## Timed boot log capture

After flash, capture N seconds of serial output to `logs/flash-{demo}-{timestamp}.txt` (uses `idf.py monitor`, falls back to pyserial at 115200):

```bash
python scripts/flash.py --demo x02 --port COM4 --monitor-seconds 30
make flash DEMO=x02 PORT=COM4   # then add --monitor-seconds via scripts/flash.py directly
```

Combine with `--monitor` to save the log file, then stay attached interactively:

```bash
python scripts/flash.py --demo x02 --port COM4 --monitor-seconds 20 --monitor
```

Paste relevant excerpts from the saved log into session notes (login HTTP status, state transitions).

## Boot checklist

- [ ] WiFi join log or ST_WIFI_ERR screen
- [ ] Connecting dots animate smoothly (not frozen 15 s)
- [ ] Roster appears after server probe

## PIN checklist

- [ ] 4 digits → `checking...` within 1 tap
- [ ] Within 3 s: carousel OR `wrong pin` OR connecting (never infinite checking)

## Carousel checklist

- [ ] Tap non-center card → animates to center
- [ ] Play centered message → after end, same card still centered

## Evidence

Paste serial excerpt showing login HTTP status or state transitions into session notes.

## Failure handling

If any step fails, do not apply unrelated fixes in the same commit. Capture logs first; apply a single-purpose fix before re-flashing.
