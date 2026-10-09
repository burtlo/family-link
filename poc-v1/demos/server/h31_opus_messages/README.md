# h31 — Opus chunk messages (Phase 3)

Minimal [`MESSAGE-PROTOCOL.md`](../../../docs/MESSAGE-PROTOCOL.md) subset: UUID message IDs, 2 s Opus chunk PUTs (length-prefixed packet stream, same as h30), finalize to **Ogg Opus** on disk.

## Run

```bash
python scripts/run_server_demo.py h31_opus_messages
make demo-opus-messages
```

Bearer auth accepts tokens from **`devices.local.yaml`** / **`devices.example.yaml`** (`change-me-a`, …) **or** **`hangout.local.yaml`** / **`hangout.example.yaml`** endpoint tokens (`change-me-mazi`, …). Match `DEMO_DEVICE_TOKEN` in `firmware/secrets.h` to one of those values, then **reflash** after changing secrets.

## Finalize / seek

- **Mux:** Pure Python `ogg_mux.py` streams chunk files without loading the full message into RAM. **ffprobe** (when installed) cross-checks `duration_ms` after mux.
- **Fallback:** If ffmpeg/ffprobe are absent, mux and range GET still work; duration comes from frame count × 20 ms.
- **Device playback:** Firmware uses **`GET …/index.json`** seek points (time_ms → byte_offset in `media.ogg`) plus HTTP **Range** resume. Full in-device Ogg parse is limited to sequential page walk from the ranged offset (no libogg).

## Size comparison (stub)

| Codec | ~3 min encoded (est.) | Notes |
|---|---|---|
| PCM s16le 16 kHz | ~5.76 MB | Protocol baseline |
| Opus 16 kbps VOIP | ~360–400 KB + Ogg overhead | Measured per-chunk bytes in `client.py` PASS line |
| Opus 24 kbps AUDIO | ~540 KB + Ogg overhead | h31 Boot toggles profile on device |
